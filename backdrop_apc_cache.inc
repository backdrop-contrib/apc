<?php
/**
 * @file
 * This integrates the Backdrop APC cache backend.
 */

// Create dummy functions to prevent fatal errors.
if (!function_exists('apc_fetch')) {
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
 * This is Backdrop's APC cache implementation. It uses Alternative PHP
 * Cache to store cached data. Each cache bin corresponds to a prefix of
 * the apc variables with the same name.
 */
class BackdropAPCCache implements BackdropCacheInterface {
  /**
   * @var string
   */
  protected $bin;

  /**
   * @var string
   */
  protected $prefix;

  /**
   * @var boolean
   */
  protected $drush;

  /**
   * @var array
   */
  protected static $remoteClears = array();

  /**
   * Get prefix for bin using the configuration.
   *
   * @param string $bin
   *
   * @return string
   *   Can be an empty string, if no prefix set.
   */
  protected static function getPrefixSettingForBin($bin) {
    $prefixes = settings_get('cache_prefix', '');

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

  function __construct($bin) {
    $this->bin = $bin;
    $this->drush = (backdrop_is_cli() && function_exists('drush_log'));

    // First we determine the prefix from a setting.
    $prefix = self::getPrefixSettingForBin($this->bin);

    // If we do not have a configured prefix we use the HTTP_HOST.
    if (empty($prefix) && isset($_SERVER['HTTP_HOST'])) {
      // Provide a fallback for multisite. This is on purpose not inside the
      // getPrefixForBin() function in order to decouple the unified prefix
      // variable logic and custom module related security logic, that is not
      // necessary for all backends.
      $prefix = $_SERVER['HTTP_HOST'] . '::';
    } else {
      $prefix = $prefix . '::';
    }

    // When we are in testing mode we add the test prefix.
    if ($test_prefix = backdrop_valid_test_ua()) {
      $prefix = $test_prefix . '::' . $prefix;
    }
    else if (isset($GLOBALS['backdrop_test_info'])) {
      $prefix = $GLOBALS['backdrop_test_info']['test_run_id'] . '::' . $prefix;
    }

    $this->prefix = $prefix;
  }

  /**
   * Function which retrieves the safe key for the cache bin.
   *
   * @return
   *   The safe APC key.
   */
  private function binKey() {
    return $this->prefix . $this->bin . '::';
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
    return $this->binKey() . $cid;
  }

  function get($cid) {
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
    if (!isset($cache->data)) {
      return FALSE;
    }

    // If enforcing a minimum cache lifetime, validate that the data is
    // currently valid for this user before we return it by making sure the cache
    // entry was created before the timestamp in the current session's cache
    // timer. The cache variable is loaded into the $user object by _backdrop_session_read()
    // in session.inc. If the data is permanent or we're not enforcing a minimum
    // cache lifetime always return the cached data.
    global $user;
    if ($cache->expire != CACHE_PERMANENT && config_get('system.core','page_cache_maximum_age', 0) && (isset($user->cache) && $user->cache > $cache->created)) {
      // This cache data is too old and thus not valid for us, ignore it.
      return FALSE;
    }

    return $cache;
  }

  function getMultiple(array &$cids) {
    if (!$cids) {
      return array();
    }
    // We need to search the cache with the proper keys and
    // be able to get the original $cid back.
    foreach ($cids as $cid) {
      $keys[$this->key($cid)] = $cid;
    }

    $fetch = apc_fetch(array_keys($keys));
    $cache = array();
    if (!empty($fetch)) {
        foreach ($fetch as $key => $data) {
          $cache[$keys[$key]] = $this->prepareItem($fetch[$key]);
        }
    }
    unset($fetch);

    // Add a get to our statistics.
    $GLOBALS['apc_statistics'][] = array('get', $this->bin, $cids);

    $cids = array_diff($cids, array_keys($cache));

    return $cache;
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
        if (config_get('system.core','page_cache_maximum_age', 0) > 0) {
          $set_result = apc_store($this->key($cid), $cache, config_get('system.core','page_cache_maximum_age', 0));
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

  /**
   * Delete CID matching the given prefix.
   *
   * @param string $prefix
   */
  function deletePrefix($prefix) {
    if (class_exists('APCIterator')) {
      $iterator = new APCIterator('user', '/^\Q' . $this->binKey() . $prefix . '\E/', APC_ITER_KEY);
      foreach ($iterator as $key => $data) {
        apc_delete($key);
       }
    }
   }

  /**
   * Flush all cache items in a bin.
   */
  function flush() {
    $this->deletePrefix('');
  }

  function clear($cid = NULL, $wildcard = FALSE) {
    if ($this->drush) {
      self::$remoteClears[$this->bin][serialize($cid)] = $wildcard;
      // APC uses separate storage for CLI mode, bounce the clear request back
      // into this method on all server nodes via XMLRPC.
      return;
    }

    // Add a get to our statistics.
    $GLOBALS['apc_statistics'][] = array('clear', $this->bin, $cid, (int)$wildcard);

    if (empty($cid)) {
      $this->flush();
    }
    else {
      if ($wildcard) {
        if ($cid == '*') {
          $this->flush();
        }
        else {
          $this->deletePrefix($cid);
        }
      }
      else if (is_array($cid)) {
        foreach ($cid as $entry) {
          apc_delete($this->key($entry));
        }
      }
      else {
        apc_delete($this->key($cid));
      }
    }
  }

  function isEmpty() {
    if (class_exists('APCIterator')) {
      $iterator = new APCIterator('user', '/^\Q' . $this->binKey() . '\E/', APC_ITER_KEY);
      return 0 === $iterator->getTotalCount();
    }
    return TRUE;
  }

  public static function remoteFlush() {
    if (!module_exists('apc')) {
      drush_log('You need to enable the APC module for remote cache clearing to work. Run drush pm-enable apc.', 'error');
      return;
    }

    global $base_url;
    if (!empty(self::$remoteClears)) {
      // optimize '*' clears.
      $star = serialize('*');
      foreach (self::$remoteClears as $bin => $clears) {
        if (!empty($clears[$star])) {
          self::$remoteClears[$bin] = array($star => TRUE);
        }
      }
      $args = array(
        'apc_drush_flush' => array(array(
          'clears' => self::$remoteClears,
          'cron_key' => state_get('cron_key', 'backdrop'),
        )),
      );
      $uri = $base_url . '/xmlrpc.php';
      $response = xmlrpc($uri, $args);

      if ($response === FALSE) {
        drush_log('xmlrpc() error: (' . xmlrpc_errno() . ') ' . xmlrpc_error_msg(), 'error');
        if ($base_url == 'http://' . basename(conf_path())) {
          drush_log('The base_url might not be set correctly try using the -l/--uri option for drush.', 'warning');
        }
      }
      elseif (!$response['success']) {
        drush_log('APC could not flush cache(s) because ' . $apc_node . ' returned code ' . $response['message'], 'error');
      }
      else {
        drush_log("APC-Remote $apc_node: {$response['message']}", 'success');
      }
    }
  }
  /**
   * Implements BackdropCacheInterface::delete().
   */
  function delete($cid) {
  }
  /**
   * Implements BackdropCacheInterface::deleteMultiple().
   */
  function deleteMultiple(array $cids) {
  }
  /**
   * Implements BackdropCacheInterface::garbageCollection().
   */
  function garbageCollection() {
  }
}
