# NGINX Variable Quick Reference

This page lists every NGINX variable provided by `ngx_stream_radius_module`.

## Packet Variables

| Variable | Returns |
|---|---|
| `$radius_valid` | `1` = parsed OK, `0` = parse error |
| `$radius_code` | Numeric code (e.g., `1`) |
| `$radius_code_name` | Text code (e.g., `Access-Request`) |
| `$radius_identifier` | Packet identifier byte |
| `$radius_length` | Declared packet length |
| `$radius_authenticator` | Authenticator as 32-char hex |

## Standard Attributes (RFC 2865)

| Variable | Attr | Type |
|---|---|---|
| `$radius_attr_User_Name` | 1 | string |
| `$radius_attr_User_Password` | 2 | octets (encrypted, hex) |
| `$radius_attr_CHAP_Password` | 3 | octets |
| `$radius_attr_NAS_IP_Address` | 4 | ipaddr |
| `$radius_attr_NAS_Port` | 5 | integer |
| `$radius_attr_Service_Type` | 6 | integer |
| `$radius_attr_Framed_Protocol` | 7 | integer |
| `$radius_attr_Framed_IP_Address` | 8 | ipaddr |
| `$radius_attr_Framed_IP_Netmask` | 9 | ipaddr |
| `$radius_attr_Framed_Routing` | 10 | integer |
| `$radius_attr_Filter_Id` | 11 | string |
| `$radius_attr_Framed_MTU` | 12 | integer |
| `$radius_attr_Framed_Compression` | 13 | integer |
| `$radius_attr_Login_IP_Host` | 14 | ipaddr |
| `$radius_attr_Login_Service` | 15 | integer |
| `$radius_attr_Login_TCP_Port` | 16 | integer |
| `$radius_attr_Reply_Message` | 18 | string |
| `$radius_attr_Callback_Number` | 19 | string |
| `$radius_attr_Callback_Id` | 20 | string |
| `$radius_attr_Framed_Route` | 22 | string |
| `$radius_attr_Framed_IPX_Network` | 23 | ipaddr |
| `$radius_attr_State` | 24 | octets |
| `$radius_attr_Class` | 25 | octets |
| `$radius_attr_Vendor_Specific` | 26 | octets (raw) |
| `$radius_attr_Session_Timeout` | 27 | integer |
| `$radius_attr_Idle_Timeout` | 28 | integer |
| `$radius_attr_Termination_Action` | 29 | integer |
| `$radius_attr_Called_Station_Id` | 30 | string |
| `$radius_attr_Calling_Station_Id` | 31 | string |
| `$radius_attr_NAS_Identifier` | 32 | string |
| `$radius_attr_Proxy_State` | 33 | octets |
| `$radius_attr_Login_LAT_Service` | 34 | string |
| `$radius_attr_Login_LAT_Node` | 35 | string |
| `$radius_attr_Login_LAT_Group` | 36 | octets |
| `$radius_attr_Framed_AppleTalk_Link` | 37 | integer |
| `$radius_attr_Framed_AppleTalk_Network` | 38 | integer |
| `$radius_attr_Framed_AppleTalk_Zone` | 39 | string |
| `$radius_attr_CHAP_Challenge` | 60 | octets |
| `$radius_attr_NAS_Port_Type` | 61 | integer |
| `$radius_attr_Port_Limit` | 62 | integer |
| `$radius_attr_Login_LAT_Port` | 63 | string |
| `$radius_attr_Tunnel_Type` | 64 | integer |
| `$radius_attr_Tunnel_Medium_Type` | 65 | integer |
| `$radius_attr_Connect_Info` | 77 | string |
| `$radius_attr_Message_Authenticator` | 80 | octets |
| `$radius_attr_NAS_Port_Id` | 87 | string |
| `$radius_attr_Framed_Pool` | 88 | octets |
| `$radius_attr_NAS_IPv6_Address` | 95 | ipv6addr |

## Accounting Attributes (RFC 2866)

| Variable | Attr | Type |
|---|---|---|
| `$radius_attr_Acct_Status_Type` | 40 | integer |
| `$radius_attr_Acct_Delay_Time` | 41 | integer |
| `$radius_attr_Acct_Input_Octets` | 42 | integer |
| `$radius_attr_Acct_Output_Octets` | 43 | integer |
| `$radius_attr_Acct_Session_Id` | 44 | string |
| `$radius_attr_Acct_Authentic` | 45 | integer |
| `$radius_attr_Acct_Session_Time` | 46 | integer |
| `$radius_attr_Acct_Input_Packets` | 47 | integer |
| `$radius_attr_Acct_Output_Packets` | 48 | integer |
| `$radius_attr_Acct_Terminate_Cause` | 49 | integer |
| `$radius_attr_Acct_Multi_Session_Id` | 50 | string |
| `$radius_attr_Acct_Link_Count` | 51 | integer |
| `$radius_attr_Acct_Input_Gigawords` | 52 | integer |
| `$radius_attr_Acct_Output_Gigawords` | 53 | integer |
| `$radius_attr_Event_Timestamp` | 55 | date (UNIX epoch) |

## Vendor-Specific Variables

Pattern: `$radius_vsa_<VendorName>_<AttributeName>`

Requires `radius_dict` loading the vendor dictionary.

### Cisco (PEN 9) — `dictionary.cisco`

| Variable | Sub-type |
|---|---|
| `$radius_vsa_Cisco_Cisco-AVPair` | 1 |
| `$radius_vsa_Cisco_Cisco-NAS-Port` | 2 |
| `$radius_vsa_Cisco_Cisco-Call-Type` | 21 |
| `$radius_vsa_Cisco_Cisco-Policy-Up` | 37 |
| `$radius_vsa_Cisco_Cisco-Policy-Down` | 38 |
| `$radius_vsa_Cisco_Cisco-Idle-Limit` | 244 |
| `$radius_vsa_Cisco_Cisco-Subscriber-Password` | 249 |

### MikroTik (PEN 14988) — `dictionary.mikrotik`

| Variable | Sub-type |
|---|---|
| `$radius_vsa_Mikrotik_Mikrotik-Recv-Limit` | 1 |
| `$radius_vsa_Mikrotik_Mikrotik-Xmit-Limit` | 2 |
| `$radius_vsa_Mikrotik_Mikrotik-Group` | 3 |
| `$radius_vsa_Mikrotik_Mikrotik-Rate-Limit` | 8 |
| `$radius_vsa_Mikrotik_Mikrotik-Realm` | 9 |
| `$radius_vsa_Mikrotik_Mikrotik-Host-IP` | 10 |
| `$radius_vsa_Mikrotik_Mikrotik-Address-List` | 19 |

## Dynamic Lookup

Any attribute can be accessed by number or dictionary name at runtime:

```nginx
$radius_attr_1          # → User-Name value
$radius_attr_User-Name  # → same (name lookup)
$radius_attr_user_name  # → same (case-insensitive, hyphen=underscore)
```
