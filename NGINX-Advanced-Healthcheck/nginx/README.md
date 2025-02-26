## NGINX Configuration for advanced healtcheck

- [`conf.d`](conf.d) contains:
  - The main configuration to run advanced healtcheck (`healtcheck.conf` and `healthcheck.js`) - this works for HTTP(S) and TCP/UDP upstreams
  - A sample load balancer configuration (upstream & server) for HTTP(S) services (`loadbalancer.conf`)
- [`stream-conf.d`](stream-conf.d) contains:
  - A sample load balancer configuration (upstream & server) for TCP/UDP services (`stream-loadbalancer.conf`)
