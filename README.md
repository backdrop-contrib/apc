## Backdrop APC

This is a port of Drupal's [APC Module](https://drupal.org/project/apc "APC - Alternative PHP Cache") to [Backdrop CMS](https://backdropcms.org).

## Description

The [APC - Alternative PHP Cache](http://www.php.net/apc) is a free and open opcode cache for PHP. Its goal is to provide a free, open, and robust framework for caching and optimizing PHP intermediate code. Besides a opcode cache it provides a user cache for storing application data. This module uses the APC user cache as a cache backend for Backdrop.

Use APC for caches that do not change often and will not grow too big to avoid fragmentation. The default setting of APC will allow you to store 32 MiB for the opcode cache and the user cache combined. Make sure you tweak this according to your website's needs. An example configuration could be to cache 'cache' and 'cache_bootstrap' in APC, 'cache_field' and 'cache_menu' in Memcached, and 'cache_filter' in the database.

### Installation

##### Step 1

Install like any other Backdrop module. See [Backdrop's Official Guide](https://backdropcms.org/guide/modules) for more information.

Enable the module and make sure the APC extension is installed properly on the status page, e.g. `http://yoursite/admin/reports/status`.

Step 2 is important to think about because it can make your site faster or slower depending on the right configuration. APC normally has a limited memory allocation (32M) and should be used to cache the entries which are used most since it's the cache closest (and maybe fastest) to PHP. When the memory allocated by APC is big enough to cache the entire Backdrop cache (to do so check the size of the *cache_* tables in the database when the cache is hot) you can use **Step 2b**, if not use **Step 2a**.

##### Step 2a (Option 1)

Add the following code to your *settings.php* file:

```php
/**
 * Add APC Caching.
 */
$settings['cache_backends'][] = 'modules/apc/backdrop_apc_cache.inc';
$settings['cache_class_cache'] = 'BackdropAPCCache';
$settings['cache_class_cache_bootstrap'] = 'BackdropAPCCache';
//$settings['apc_show_debug'] = TRUE;  // Remove the slashes to use debug mode.
```

##### Step 2b (Option 2)

Add the following code to your *settings.php* file:

```php
/**
 * Add APC Caching.
 */
$settings['cache_backends'][] = 'modules/apc/backdrop_apc_cache.inc';
$settings['cache_default_class'] = 'BackdropAPCCache';
//$settings['apc_show_debug'] = TRUE;  // Remove the slashes to use debug mode.
```

##### Step 3

Visit your site to see if it's still working!

##### Step 4 (Optional)

When using BackdropAPCCache as default or manually caching the 'cache_page' bin in your settings file you do not need to start the database because Backdrop can use the APC cache for pages. Add the following code to your *settings.php* file to do so:

```php
$settings['page_cache_without_database'] = TRUE;
$settings['page_cache_invoke_hooks'] = FALSE;
```

##### Step 5 (Optional)

Visit your site to see if it's still working!

### Testing

To be able to test this module, open *core/includes/cache.inc* and search for `settings_get('cache_default_class', 'BackdropDatabaseCache')`. and change this to BackdropAPCCache. This is because the `$settings['']` array in *settings.php* is not always loaded properly.

## Credits

Drupal module authors and maintainers:

* Drupal 7 - [R. Muilwijk](https://drupal.org/user/159883) (Raymond Muilwijk)
* Drupal 5 & 6 - [slantview](https://www.drupal.org/user/73183) (Steve Rude)
