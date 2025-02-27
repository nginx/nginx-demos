# NGINX Gateway Fabric Demo

This demo contains the files used in the NGINX Gateway Fabric AppWorld demo.

To install the Gateway API CRDs:

```shell
kubectl kustomize "https://github.com/nginx/nginx-gateway-fabric/config/crd/gateway-api/standard?ref=v1.6.1" | kubectl apply -f -
```

To install NGINX Gateway Fabric:

```shell
helm install ngf oci://ghcr.io/nginx/charts/nginx-gateway-fabric --create-namespace -n nginx-gateway
```
