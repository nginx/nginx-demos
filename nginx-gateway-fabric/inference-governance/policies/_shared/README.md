Test fixtures shared by every policy test (`clusterResources` in each kyverno-test.yaml).

Note on Kyverno CLI v1.19.2: when a test sets `clusterResources`, `resource.Get()` reads from the
fake cluster built from those resources, not from a `context:` file. So everything a policy looks
up (ConfigMaps, InferencePools, Namespaces) lives in cluster.yaml.
