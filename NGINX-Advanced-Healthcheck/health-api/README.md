## Health REST API

This is a sample Python REST API that returns load and system information for the host it runs on.
It is used by this repository to read upstreams CPU usage and set the upstream server status based on the realtime CPU load.

To run:

```
$ pip install -r requirements.txt
[...]
$ python serverstats.py 
 * Serving Flask app 'serverstats'
 * Debug mode: off
WARNING: This is a development server. Do not use it in a production deployment. Use a production WSGI server instead.
 * Running on all addresses (0.0.0.0)
 * Running on http://127.0.0.1:5000
 * Running on http://192.168.1.19:5000
Press CTRL+C to quit
```

It responds to HTTP `GET /stats` requests on port 5000:

```
$ curl -i http://127.0.0.1:5000/stats
HTTP/1.1 200 OK
Server: Werkzeug/2.2.2 Python/3.10.9
Date: Mon, 19 Dec 2022 16:47:17 GMT
Content-Type: application/json
Content-Length: 1836
Connection: close

{"cpu":{"arch":"X86_64","arch_string_raw":"x86_64","bits":64,"brand_raw":"Intel(R) Core(TM) i7-4900MQ CPU @ 2.80GHz","count":8,"cpuinfo_version":[9,0,0],"cpuinfo_version_string":"9.0.0","family":6,"flags":["abm","acpi","aes","aperfmperf","apic","arat","arch_perfmon","avx","avx2","bmi1","bmi2","bts","clflush","cmov","constant_tsc","cpuid","cpuid_fault","cx16","cx8","de","ds_cpl","dtes64","dtherm","dts","epb","ept","ept_ad","erms","est","f16c","flexpriority","fma","fpu","fsgsbase","fxsr","hle","ht","ida","invpcid","invpcid_single","lahf_lm","lm","mca","mce","mmx","monitor","movbe","msr","mtrr","nonstop_tsc","nopl","nx","osxsave","pae","pat","pbe","pcid","pclmulqdq","pdcm","pdpe1gb","pebs","pge","pln","pni","popcnt","pse","pse36","pti","pts","rdrand","rdrnd","rdtscp","rep_good","rtm","sdbg","sep","smep","smx","ss","sse","sse2","sse4_1","sse4_2","ssse3","syscall","tm","tm2","tpr_shadow","tsc","tsc_adjust","tscdeadline","vme","vmx","vnmi","vpid","x2apic","xsave","xsaveopt","xtopology","xtpr"],"hz_actual":[3192955000,0],"hz_actual_friendly":"3.1930 GHz","hz_advertised":[2800000000,0],"hz_advertised_friendly":"2.8000 GHz","l1_data_cache_size":131072,"l1_instruction_cache_size":131072,"l2_cache_associativity":6,"l2_cache_line_size":256,"l2_cache_size":1048576,"l3_cache_size":8388608,"load":{"15minute":37.14599609375,"1minute":25.927734375,"5minute":35.4248046875},"model":60,"python_version":"3.10.9.final.0 (64 bit)","stepping":3,"vendor_id_raw":"GenuineIntel"},"disk":{"/":{"free":177138003968,"percent":65.2,"total":509677637632,"used":332539633664}},"swapMemory":{"total":2147479552},"system":{"bootTime":1671436837.0},"virtualMemory":{"active":2608058368,"available":22842421248,"buffers":156848128,"cached":10918633472,"free":14635302912,"inactive":13349625856,"percent":31.4,"total":33295020032,"used":7584235520}}```
```
