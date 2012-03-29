<?php
/**
 * @file
 * This integrates the drupal APC cache backend.
 */

// The cli mode does not run the same php.ini file and could not have
// loaded the apc functions. Create dummy's to prevent fatal errors.
if (drupal_is_cli() && !function_exists('apc_fetch')) {
  function apc_fetch($key, $success = NULL) {
    return FALSE;
  }

  function apc_store($key, $var, $ttl = 0) {
    return FALSE;
  }
}

// Array to store statistics about the current page apc calls.
$apc_statistics = array();

/**
 * APC cache implementation.
 *
 * This is Drupal's APC cache implementation. It uses Alternative PHP
 * Cache to store cached data. Each cache bin corresponds to a prefix of
 * the apc variables with the same name.
 */
class DrupalAPCCache implements DrupalCacheInterface {
  /**
   * @var string
   */
  protected $bin;

  /**
   * @var string
   */
  protected $prefix;

  /**
   * Get prefix for bin using the configuration.
   *
   * @param string $bin
   *
   * @return string
   *   Can be an empty string, if no prefix set.
   */
  protected static function getPrefixForBin($bin) {
    if (isset($GLOBALS['drupal_test_info']) && !empty($test_info['test_run_id'])) {
      return $test_info['test_run_id'];
    } else {
      $prefixes = variable_get('cache_prefix', '');

      if (is_string($prefixes)) {
        // Variable can be a string, which then considered as a default behavior.
        return $prefixes;
      }

      if (isset($prefixes[$bin])) {
        if (FALSE !== $prefixes[$bin]) {
          // If entry is set and not FALSE, an explicit prefix is set for the bin.
          return $prefixes[$bin];
        } else {
          // If we have an explicit false, it means no prefix whatever is the
          // default configuration.
          return '';
        }
      } else {
        // Key is not set, we can safely rely on default behavior.
        if (isset($prefixes['default']) && FALSE !== $prefixes['default']) {
          return $prefixes['default'];
        } else {
          // When default is not set or an explicit FALSE, this means no prefix.
          return '';
        }
      }
    }
  }

  function __construct($bin) {
    $this->bin = $bin;

    $prefix = self::getPrefixForBin($this->bin);

    if (empty($prefix) && isset($_SERVER['HTTP_HOST'])) {
      // Provide a fallback for multisite. This is on purpose not inside the
      // getPrefixForBin() function in order to decouple the unified prefix
      // variable logic and custom module related security logic, that is not
      // necessary for all backends.
      $this->prefix = $_SERVER['HTTP_HOST'] . '_';
    } else {
      $this->prefix = $prefix;
    }
  }

  /**
   * Function which retrieves the safe key for the cache bin.
   *
   * @return
   *   The safe APC key.
   */
  private function binKey() {
    return $this->prefix . $this->bin;
  }

  /**
   * Function which retrieves the safe key for the cache cid.
   *
   * @param $cid
   *   The cache id.
   * @return
   *   The safe APC key.
   */
  private function key($cid) {
    return $this->binKey() . '|' . $cid;
  }

  function get($cid) {
    // Garbage collection necessary when enforcing a minimum cache lifetime.
    $this->garbageCollection($this->bin);

    // Add a get to our statistics.
    $GLOBALS['apc_statistics'][] = array('get', $this->bin, array($cid));

    // Fetch the data.
    $cache = apc_fetch($this->key($cid));
    return $this->prepareItem($cache);
  }

  /**
   * Prepare a cached item.
   *
   * Checks that items are either permanent or did not expire.
   *
   * @param $cache
   *   An item loaded from cache_get() or cache_get_multiple().
   * @return
   *   The item with data unserialized as appropriate or FALSE if there is no
   *   valid item to load.
   */
  protected function prepareItem($cache) {
    global $user;

    if (!isset($cache->data)) {
      return FALSE;
    }

    // If enforcing a minimum cache lifetime, validate that the data is
    // currently valid for this user before we return it by making sure the cache
    // entry was created before the timestamp in the current session's cache
    // timer. The cache variable is loaded into the $user object by _drupal_session_read()
    // in session.inc. If the data is permanent or we're not enforcing a minimum
    // cache lifetime always return the cached data.
    if ($cache->expire != CACHE_PERMANENT && variable_get('cache_lifetime', 0) && (isset($user->cache) && $user->cache > $cache->created)) {
      // This cache data is too old and thus not valid for us, ignore it.
      return FALSE;
    }

    return $cache;
  }

  function getMultiple(&$cids) {
    // Garbage collection necessary when enforcing a minimum cache lifetime.
    $this->garbageCollection($this->bin);

    // We need to search the cache with the proper keys and
    // be able to get the original $cid back.
    foreach ($cids as $cid) {
      $keys[$this->key($cid)] = $cid;
    }

    $fetch = apc_fetch(array_keys($keys));
    $cache = array();
    foreach ($fetch as $key => $data) {
      $cache[$keys[$key]] = $this->prepareItem($fetch[$key]);
    }
    unset($fetch);

    // Add a get to our statistics.
    $GLOBALS['apc_statistics'][] = array('get', $this->bin, $cids);

    $cids = array_diff($cids, array_keys($cache));

    return $cache;
  }

  /**
   * Garbage collection for get() and getMultiple().
   *
   * @param $bin
   *   The bin being requested.
   */
  protected function garbageCollection() {
    global $user;

    // Garbage collection necessary when enforcing a minimum cache lifetime.
    $cache_flush = variable_get('cache_flush_' . $this->bin, 0);
    if ($cache_flush && ($cache_flush + variable_get('cache_lifetime', 0) <= REQUEST_TIME)) {
      // Reset the variable immediately to prevent a meltdown in heavy load situations.
      variable_set('cache_flush_' . $this->bin, 0);
      // Time to flush old cache data
      $this->clearTemporary();
    }
  }

  function set($cid, $data, $expire = CACHE_PERMANENT, array $headers = NULL) {
    // Add set to statistics.
    $GLOBALS['apc_statistics'][] = array('set', $this->bin, $cid);

    // Create new cache object.
    $cache = new stdClass();
    $cache->cid = $cid;
    // APC will serialize any structure we give itself.
    $cache->serialized = 0;
    $cache->created = REQUEST_TIME;
    $cache->expire = $expire;
    $cache->headers = isset($headers) ? $headers : NULL;

    $cache->data = $data;

    // What kind of expiration is being used.
    switch ($expire) {
      case CACHE_PERMANENT:
        $set_result = apc_store($this->key($cid), $cache);
        break;

      case CACHE_TEMPORARY:
        if (variable_get('cache_lifetime', 0) > 0) {
          $set_result = apc_store($this->key($cid), $cache, variable_get('cache_lifetime', 0));
        }
        else {
          $set_result = apc_store($this->key($cid), $cache);
        }
        break;

      default:
        $set_result = apc_store($this->key($cid), $cache, $expire - time());
        break;
    }
  }

  function clearTemporary() {
    $data = apc_cache_info('user');
    $count = count($data['cache_list']);

    for ($i = 0; $i < $count; $i++) {
      if (strpos($data['cache_list'][$i]['info'], $this->binKey()) === 0) {
        $cache = apc_fetch($data['cache_list'][$i]['info']);
        if ($cache && $cache->expire == CACHE_TEMPORARY) {
          apc_delete($data['cache_list'][$i]['info']);
        }
      }
    }
  }

  function clear($cid = NULL, $wildcard = FALSE) {
    global $user;

    if (drupal_is_cli() && function_exists('drush_log')) {
      drush_log($this->bin . '(' . $cid . ') was not cleared. APC cli uses a different memory storage then the webserver. For more info see: http://drupal.org/node/1278232', 'warning');
      return;
    }

    // Add a get to our statistics.
    $GLOBALS['apc_statistics'][] = array('clear', $this->bin, $cid, (int)$wildcard);

    if (empty($cid)) {
      if (variable_get('cache_lifetime', 0)) {
        // We store the time in the current user's $user->cache variable which
        // will be saved into the sessions bin by _drupal_session_write(). We then
        // simulate that the cache was flushed for this user by not returning
        // cached data that was cached before the timestamp.
        $user->cache = REQUEST_TIME;

        $cache_flush = variable_get('cache_flush_' . $this->bin, 0);
        if ($cache_flush == 0) {
          // This is the first request to clear the cache, start a timer.
          variable_set('cache_flush_' . $this->bin, REQUEST_TIME);
        }
        elseif (REQUEST_TIME > ($cache_flush + variable_get('cache_lifetime', 0))) {
          // Clear the cache for everyone, cache_lifetime seconds have
          // passed since the first request to clear the cache.
          $this->clearTemporary();
          variable_set('cache_flush_' . $this->bin, 0);
        }
      }
      else {
        $this->clearTemporary();
      }
    }
    else {
      if ($wildcard) {
        $data = apc_cache_info('user');
        $count = count($data['cache_list']);

        if ($cid == '*') {
          for ($i = 0; $i < $count; $i++) {
            if (strpos($data['cache_list'][$i]['info'], $this->binKey()) === 0) {
              apc_delete($data['cache_list'][$i]['info']);
            }
          }
        }
        else {
          for ($i = 0; $i < $count; $i++) {
            if (strpos($data['cache_list'][$i]['info'], $this->key($cid)) === 0) {
              apc_delete($data['cache_list'][$i]['info']);
            }
          }
        }
      }
      elseif (is_array($cid)) {
        foreach ($cid as $delete_cid) {
          apc_delete($this->key($delete_cid));
        }
      }
      else {
        apc_delete($this->key($cid));
      }
    }
  }

  function isEmpty() {
    $data = apc_cache_info('user');
    $count = count($data['cache_list']);

    for ($i = 0; $i < $count; $i++) {
      if (strpos($data['cache_list'][$i]['info'], $this->binKey()) === 0) {
        return FALSE;
      }
    }

    return TRUE;
  }
}
