<?php
// $Id$

global $apc_page_stats;
$apc_page_stats = array (
  'get' 	=> 0,
  'set' 	=> 0,
  'hit' 	=> 0,
  'miss'	=> 0,
  'delete'	=> 0,
);


/**
 * Return data from the persistent cache.
 *
 * @param $key
 *   The key of the data to retrieve.
 * @param $bin
 *   The bin/silo to look in. Valid core values are 'cache_filter',
 *   'cache_menu', 'cache_page', or 'cache' for the default cache.
 */
function cache_get($cid, $bin = 'cache') {
  global $user;

  $cache = apc_handle_get($cid, $bin);
  if(!is_object($cache)) {
    /* Garbage collection necessary when enforcing a minimum cache lifetime
     * This only affects the database, not apc data cache
     */
    $cache_flush = variable_get('cache_flush', 0);
    if($cache_flush && ($cache_flush + variable_get('cache_lifetime', 0) <= time())) { 
      db_query("DELETE FROM {%s} WHERE expire != %d AND expire <= %d", $bin, CACHE_PERMANENT, $cache_flush);  
      variable_set('cache_flush', 0);
    }
    // Load cache object from database
    $cache = db_fetch_object(db_query("SELECT data, created, headers, expire FROM {%s} WHERE cid = '%s'", $bin, $cid));  
    if(is_object($cache) && isset($cache->data)) {       
      $cache->data = unserialize(db_decode_blob($cache->data));
      // Need to add it to memcache in the event of a memcache server disappearing and coming back
      apc_handle_set($cid,$cache, $cache->expire, $bin);
    }
  } 

  if ($cache->data) {
    if ($cache->expire != CACHE_PERMANENT || variable_get('cache_lifetime', 0)) {
      if ($user->cache > $cache->created) {
        return 0;
      }
    }

    return $cache;
  }
  return 0;
}

}

/**
 * Store data in the persistent cache.
 
 * @param $cid
 *   The cache ID of the data to store.
 * @param $bin
 *   The bin/silo to store the data in. Valid core values are 'cache_filter',
 *   'cache_menu', 'cache_page', or 'cache'.
 * @param $data
 *   The data to store in the cache.
 * @param $expire
 *   One of the following values:
 *   - CACHE_PERMANENT: Indicates that the item should never be removed unless
 *     explicitly told to using cache_clear_all() with a cache ID.
 *   - CACHE_TEMPORARY: Indicates that the item should be removed at the next
 *     general cache wipe.
 *   - A Unix timestamp: Indicates that the item should be kept at least until
 *     the given time, after which it behaves like CACHE_TEMPORARY.
 * @param $headers
 *   A string containing HTTP header information for cached pages.
 */
function cache_set($cid, $bin = 'cache', $data, $expire = CACHE_PERMANENT, $headers = NULL) {
  // Create new cache object.
  $cache = new stdClass;
  $cache->cid = $cid;
  $cache->data = $data;
  $cache->created = time();
  $cache->expire = $expire;
  $cache->headers = $headers;
  
  // If error on set, add it to the database cache
  if(!apc_handle_set($cid, $cache, $expire, $bin)) {
    db_lock_table($table);
    db_query("UPDATE {%s} SET data = %b, created = %d, expire = %d, headers = '%s' WHERE cid = '%s'", $table, serialize($data), time(), $expire, $headers, $cid);
    if (!db_affected_rows()) {
      db_query("INSERT INTO {%s} (cid, data, created, expire, headers) VALUES ('%s', %b, %d, %d, '%s')", $table, $cid, serialize($data), time(), $expire, $headers);
    }
    db_unlock_tables();
  }
}

function cache_clear_all($cid = NULL, $bin = NULL, $wildcard = FALSE) {
  $table = empty($table) ? 'cache' : $table;
  if (empty($cid)) {
    apc_handle_flush($table);
  }
  else {
    apc_handle_delete($cid, $table);
  }
}

/**
 * Return data from the apc cache.
 *
 * @param $key
 *   The key of the data to retrieve.
 * @param $bin
 *   The bin/silo to look in. 
 */
function apc_handle_get($key, $bin = 'cache') {
  global $apc_page_stats;
  $apc_page_stats['get']++;
  
  $full_key = apc_handle_key($key, $bin)
  if (isset($full_key)) {
    if (!$return = apc_fetch($full_key)) {  
      watchdog('apc', 'Failed to get key: ' . $full_key, WATCHDOG_ERROR);
      $return = FALSE;
    }
    else {
      $apc_page_stats['hits']++;
    }
  }
  else {
  	$apc_page_stats['miss']++;
    $return = FALSE; 
  }
  
  return $return;
}

/**
 * Set data into apc cache
 *
 * @param $key
 *   The key of the data to retrieve.
 * @param $bin
 *   The bin/silo to look in. 
 */
function apc_handle_set($key, $data, $expire = CACHE_PERMENANT, $bin = 'cache') {
  global $apc_page_stats;
  $apc_page_stats['set']++;
  
  if (!empty($key) && !empty($data)) {
    $full_key = apc_handle_key($key, $bin);
    if (!apc_store($full_key, $value, $expire)) {
      watchdog('memcache', 'Failed to set key: ' . $full_key, WATCHDOG_ERROR);
      $return = FALSE;
    }
    else {
      $return = TRUE;
    }
  }
  
  return $return;
}

/**
 * Delete data from the apc cache
 *
 * @param $key
 *   The key of the data to retrieve.
 * @param $bin
 *   The bin/silo to look in. 
 */
function apc_handle_delete($key, $bin = 'cache') {
  global $apc_page_stats;
  $apc_page_stats['delete']++;
  
  if (!empty($key) && !empty($data)) {
    $full_key = apc_handle_key($key, $bin);
    if (!apc_delete($full_key)) {
      watchdog('memcache', 'Failed to delete key: ' . $full_key, WATCHDOG_ERROR);
      $return = FALSE;
    }
    else {
      $return = TRUE;
    }
  }
}

/**
 * Flush data from apc cache
 *
 * @param $key
 *   The key of the data to retrieve.
 * @param $bin
 *   The bin/silo to look in. 
 */
function apc_handle_flush($bin = 'cache') {
  return apc_clear_cache('user');
}

/**
 * Return stats about the apc cache
 *
 * @param $key
 *   The key of the data to retrieve.
 * @param $bin
 *   The bin/silo to look in. 
 */
function apc_handle_stats($bin = 'cache') {
  //TODO: need to figure out what i want to do here
  return print_r(apc_cache_info('user'), TRUE);
}

/**
 * Return key for apc cache
 *
 * @param $key
 *   The key of the data to retrieve.
 * @param $bin
 *   The bin/silo to look in. 
 */
function apc_handle_key($key, $bin) {
  static $prefix;

  if (empty($prefix)) {
    $prefix = variable_get('memcache_key_prefix', '');
  }
  $full_key = ($prefix ? $prefix . '-' : '') . $bin . '-' . $key;
  
  return urlencode($full_key);
}