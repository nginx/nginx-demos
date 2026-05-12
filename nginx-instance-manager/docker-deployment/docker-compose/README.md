# Docker-compose for NGINX Instance Manager

1. Edit the `.env` file setting the docker image name, the base64-encoded license and optionally username and password
```code
NIM_IMAGE=<NGINX_INSTANCE_MANAGER_DOCKER_IMAGE_NAME>
NIM_LICENSE=<BASE64_ENCODED_LICENSE_FILE>
NIM_USERNAME=admin
NIM_PASSWORD=nimadmin
```

2. Start NGINX Instance Manager
```bash
docker compose -f docker-compose.yaml up -d
```

3. Stop NGINX Instance Manager
```bash
docker compose -f docker-compose.yaml down
```

---

## Injecting NIM configuration at runtime (no image rebuild required)

`startNIM.sh` uses helper functions (`set_nms_conf` / `set_nms_sm`) to apply values
from environment variables directly into `/etc/nms/nms.conf` and `/etc/nms/nms-sm-conf.yaml`
at container startup using `yq -i`.

To add a new config key at runtime:

### Step 1 — Add the variable to `.env`

Open `docker-compose/.env` and add your variable with a default value:

```bash
# Example: set the license mode of operation
NIM_LICENSE_MODE_OF_OPERATION=connected
```

Supported values depend on the NIM config key you are targeting.  
Refer to the [NIM configuration reference](https://docs.nginx.com/nginx-instance-manager/configuration/) for valid values.

### Step 2 — Pass the variable into the container in `docker-compose.yaml`

Under the `nim` service `environment:` block, add:

```yaml
- NIM_LICENSE_MODE_OF_OPERATION=${NIM_LICENSE_MODE_OF_OPERATION}
```

### Step 3 — Map it to the config key in `startNIM.sh`

In the `# NGINX Instance Manager configuration update` section, add one line:

```bash
set_nms_conf '.integrations.license.mode_of_operation'  NIM_LICENSE_MODE_OF_OPERATION
```

The first argument is the `yq` dot-notation path into `nms.conf`.  
The second argument is the **name** (not value) of the environment variable.

For `nms-sm-conf.yaml` keys, use `set_nms_sm` instead:

```bash
set_nms_sm '.some.key'  MY_ENV_VAR
```

### Full example — adding a new key end-to-end

| File | What to add |
|------|-------------|
| `.env` | `MY_NEW_VAR=myvalue` |
| `docker-compose.yaml` | `- MY_NEW_VAR=${MY_NEW_VAR}` under `nim → environment` |
| `startNIM.sh` | `set_nms_conf '.path.to.key'  MY_NEW_VAR` |

No image rebuild is needed — changes take effect on the next `docker compose up`.
