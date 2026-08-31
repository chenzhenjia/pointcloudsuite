# PointCloudSuite configuration

`pointcloudview.config.json` is the versioned configuration template. Copy it to one of the runtime locations below and adjust `data_directory` or the point-cloud defaults.

Resolution order:

1. `pointcloudview --config <absolute path>`
2. `PCV_CONFIG_FILE=<absolute path>`
3. `config/pointcloudview.json` beside `pointcloudview.exe`
4. The user configuration directory selected by Qt

The first start creates the user configuration template when none is available. The default data directory is `D:/Scraping_Robot_Project`; it stores `runtime_data`, `cache`, `logs`, and `exports` and may be changed to another absolute path.

All values are validated independently. Missing or invalid values leave the matching built-in default in effect and are recorded in the startup log. JSON comments are not supported.
