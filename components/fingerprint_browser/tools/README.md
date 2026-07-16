# GeoIP Component Data

`build_geoip_component_data.py` produces payload for the existing Brave Local
Data Updater component:

- `dbip-city-lite.mmdb` from DB-IP Lite City MMDB.
- `geonames-city-timezones.tsv` from GeoNames `cities500.zip`.
- `geonames-country-languages.tsv` from GeoNames `countryInfo.txt`.
- source licenses in `LICENSES/` for the payload's attribution notice.

The browser receives the component's ready directory and uses it directly for
GeoIP lookups. The component publisher supplies current input files, signs the
output with the provisioned Brave Local Data Updater key, and publishes it
through component updater. DB-IP Lite and GeoNames data are both CC BY 4.0;
retain their attribution in the published component metadata and notices.
