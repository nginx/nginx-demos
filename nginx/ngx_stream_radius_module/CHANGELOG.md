# Changelog

All notable changes to `ngx_stream_radius_module` are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
This project uses [Semantic Versioning](https://semver.org/).

---

## [1.0.0] — 2024-01-01

### Added

- Initial release
- RFC 2865 full packet parser (Access-Request, Accept, Reject, Challenge, Status-Server, Status-Client)
- RFC 2866 full accounting packet parser (Accounting-Request, Accounting-Response)
- RFC 3576 / RFC 5176 packet code recognition (CoA, Disconnect)
- All RFC 2865 standard attributes pre-registered as NGINX variables (`$radius_attr_*`)
- All RFC 2866 accounting attributes pre-registered as NGINX variables (`$radius_attr_Acct_*`)
- Vendor-Specific Attribute (VSA, type 26) decoder per RFC 2865 §5.26
- FreeRADIUS-compatible dictionary file loader (`radius_dict` directive)
- `VENDOR`, `BEGIN-VENDOR`, `END-VENDOR`, `ATTRIBUTE`, `VALUE` dictionary keywords
- Dynamic variable resolution by numeric code (`$radius_attr_1`) and name (`$radius_attr_User-Name`)
- Dynamic VSA variable resolution (`$radius_vsa_<Vendor>_<Attr>`)
- Packet-level variables: `$radius_code`, `$radius_code_name`, `$radius_identifier`, `$radius_length`, `$radius_authenticator`, `$radius_valid`
- `radius_parse on|off` directive
- `radius_secret` directive (stored for future HMAC-MD5 verification)
- `radius_buffer_size` directive
- `radius_var_prefix` and `radius_vendor_var_prefix` directives
- Support for attribute data types: `string`, `integer`, `ipaddr`, `ipv6addr`, `ipv6prefix`, `date`, `octets`, `ether`, `ifid`, `integer64`
- Bundled vendor dictionaries: Cisco (PEN 9), MikroTik (PEN 14988)
- Dynamic module (`--add-dynamic-module`) support
- Static module (`--add-module`) support
- Compatible with NGINX Open Source ≥ 1.11.5 and NGINX Plus ≥ R11
- Standalone unit test suite (no NGINX headers required)
- Example NGINX configurations for common use cases

### Security

- `$radius_attr_User_Password` stored as hex-encoded encrypted bytes, never cleartext
- Dictionary files validated at configuration load time
- Packet length validated before any attribute parsing
- Malformed AVPs (zero-length, overrunning packet boundary) rejected with error logging
