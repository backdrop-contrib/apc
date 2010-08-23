- Step 1
Enable the module and make sure the APC extension is installed properly on
the status page (http://yoursite/admin/reports/status).

- Step 2
Add the following code to your settings.php file:

/**
 * Add APC Caching.
 * 
 * To use APC Caching for the 'cache_bootstrap' bin we need to include
 * the backends because Registry is not setup yet. The registry checks
 * or it has to load the file so they will not get loaded twice.
 */
$conf['cache_backends'] = array('sites/all/modules/apc/drupal_apc_cache.inc');
$conf['cache_class_cache'] = 'DrupalAPCCache';
$conf['cache_class_cache_bootstrap'] = 'DrupalAPCCache';
//$conf['apc_show_debug'] = TRUE;  // Remove the slashes to use debug mode.

- Step 3
Visit your site to see or it's still working!
