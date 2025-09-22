# NGINX AI Proxy

## Demo Overview

Simple demo showcasing how to use NGINX and NGINX JavaScript (NJS) to act as a simple AI proxy. This demo covers how to use NGINX to provide the following AI proxy capabilities:

- User-based AI model access control.
- AI model abstraction (OpenAI ↔ Anthropic) with request/response translation.
- Per-model failover.
- AI model token usage extraction into access logs.

This demo has the following limitations:

- The JSON config is statically loaded (no dynamic reload logic here).
- Only a subset of OpenAI → Anthropic fields are properly translated (enough for basic prompts).
- No handling of AI streaming.
- Authentication is done via header-based user identification (`X-User`); there is no actual auth.
- Failover only triggers on non-200 HTTP status.
- No rate limiting or caching.

## Demo Walkthrough

### Prerequisites

Before you can run this demo, you will need:

- An OpenAI API key exported as an environment variable:

    ```bash
    export OPENAI_API_KEY=<API_KEY>
    ```

- An Anthropic API key exported as an environment variable:

    ```bash
    export ANTHROPIC_API_KEY=<API_KEY>
    ```

- A functional Docker installation.

### Launching the Container Demo Environment on Docker

1. Clone this repo and change directory to the AI proxy directory inside the cloned repo:

    ```bash
    git clone https://github.com/nginx/nginx-demos
    cd nginx-demos/nginx/ai-proxy
    ```

2. Create a persistent volume for generated key snippets:

    ```bash
    docker volume create nginx-keys
    ```

3. Launch the Docker NGINX container with all the necessary configuration settings:

    ```bash
    docker run -it --rm -p 4242:4242 \
      -v $(pwd)/config:/etc/nginx \
      -v $(pwd)/njs:/etc/njs \
      -v $(pwd)/templates:/etc/nginx-ai-proxy/templates \
      -v nginx-keys:/etc/nginx-ai-proxy/keys \
      -e NGINX_ENVSUBST_TEMPLATE_DIR=/etc/nginx-ai-proxy/templates \
      -e NGINX_ENVSUBST_OUTPUT_DIR=/etc/nginx-ai-proxy/keys \
      -e OPENAI_API_KEY \
      -e ANTHROPIC_API_KEY \
      --name nginx-ai-proxy \
      nginx:1.29.1
    ```

The official NGINX image entrypoint runs `envsubst` on templates and creates an `openai-key.conf` and `anthropic-key.conf` NGINX config files under `/etc/nginx-ai-proxy/keys/` which are then `included` by the `aiproxy.conf` NGINX config file.

### Testing Basic Requests

1. Try sending a request as `user-a` to the OpenAI model:

    ```bash
    curl -s -X POST http://localhost:4242/v1/chat/completions \
      -H 'Content-Type: application/json' \
      -H 'X-User: user-a' \
      -d '{"model":"gpt-5","messages":[{"role":"user","content":"Hello"}]}'
    ```

    Expected response:

    ```json
    {
      "id": "...",
      "object": "chat.completion",
      "created": ...,
      "model": "gpt-5-2025-08-07",
      "choices": [
        {
          "index": 0,
          "message": {
            "role": "assistant",
            "content": "Hello! How can I help you today?",
            "refusal": null,
            "annotations": []
          },
          "finish_reason": "stop"
        }
      ],
      "usage": {
        "prompt_tokens": 7,
        "completion_tokens": 82,
        "total_tokens": 89,
        "prompt_tokens_details": {
          "cached_tokens": 0,
          "audio_tokens": 0
        },
        "completion_tokens_details": {
          "reasoning_tokens": 64,
          "audio_tokens": 0,
          "accepted_prediction_tokens": 0,
          "rejected_prediction_tokens": 0
        }
      },
      "service_tier": "default",
      "system_fingerprint": null
    }
    ```

2. Send a different request as `user-a` to the Anthropic model (still using the OpenAI schema as the AI model translation happens server-side in the NJS code):

    ```bash
    curl -s -X POST http://localhost:4242/v1/chat/completions \
      -H 'Content-Type: application/json' \
      -H 'X-User: user-a' \
      -d '{"model":"claude-sonnet-4-20250514","messages":[{"role":"user","content":"Hello"}]}'
    ```

    Expected response:

    ```json
    {
      "id": "...",
      "object": "chat.completion",
      "model": "claude-sonnet-4-20250514",
      "choices": [
        {
          "index": 0,
          "finish_reason": "end_turn",
          "message": {
            "role": "assistant",
            "content": "Hello! How are you doing today? Is there anything I can help you with?"
          }
        }
      ],
      "usage": {
        "prompt_tokens": 8,
        "completion_tokens": 20,
        "total_tokens": 28
      }
    }
    ```

3. Send a request as `user-b`. This user does not have access to Anthropic:

    ```bash
    curl -s -X POST http://localhost:4242/v1/chat/completions \
      -H 'Content-Type: application/json' \
      -H 'X-User: user-b' \
      -d '{"model":"claude-sonnet-4-20250514","messages":[{"role":"user","content":"Hello"}]}'
    ```

    Expected response:

    ```json
    {
      "error": {
        "message": "The model 'claude-sonnet-4-20250514' was not found or is not accessible to the user"
      }
    }
    ```

### Testing the Failover Mechanism

1. Stop the previous running NGINX AI proxy Docker container. It should automatically get deleted from your container cache:

    ```bash
    docker stop nginx-ai-proxy
    ```

2. Start a new Docker container with an invalid OpenAI key to force failure:

    ```bash
    docker run -it --rm -p 4242:4242 \
      -v $(pwd)/config:/etc/nginx \
      -v $(pwd)/njs:/etc/njs \
      -v $(pwd)/templates:/etc/nginx-ai-proxy/templates \
      -v nginx-keys:/etc/nginx-ai-proxy/keys \
      -e NGINX_ENVSUBST_TEMPLATE_DIR=/etc/nginx-ai-proxy/templates \
      -e NGINX_ENVSUBST_OUTPUT_DIR=/etc/nginx-ai-proxy/keys \
      -e OPENAI_API_KEY=bad \
      -e ANTHROPIC_API_KEY \
      --name nginx-ai-proxy \
      nginx:1.29.1
    ```

3. Send a request as `user-a` to the OpenAI model. `user-a` has configured Anthropic as a failover model:

    ```bash
    curl -s -X POST http://localhost:4242/v1/chat/completions \
      -H 'Content-Type: application/json' \
      -H 'X-User: user-a' \
      -d '{"model":"gpt-5","messages":[{"role":"user","content":"Hello"}]}'
    ```

    Expected response:

    ```json
    {
      "id": "...",
      "object": "chat.completion",
      "model": "claude-sonnet-4-20250514",
      "choices": [
        {
          "index": 0,
          "finish_reason": "end_turn",
          "message": {
            "role": "assistant",
            "content": "Hello! How are you doing today? Is there anything I can help you with?"
          }
        }
      ],
      "usage": {
        "prompt_tokens": 8,
        "completion_tokens": 20,
        "total_tokens": 28
      }
    }
    ```

4. Send a request as `user-b` to the OpenAI model. `user-b` has not failover models available:

    ```bash
    curl -s -X POST http://localhost:4242/v1/chat/completions \
      -H 'Content-Type: application/json' \
      -H 'X-User: user-b' \
      -d '{"model":"gpt-5","messages":[{"role":"user","content":"Hello"}]}'
    ```

    Expected response:

    ```json
    {
      "error": {
        "message": "Incorrect API key provided: bad. You can find your API key at https://platform.openai.com/account/api-keys.",
        "type": "invalid_request_error",
        "param": null,
        "code": "invalid_api_key"
      }
    }
    ```

Output should show `"claude-sonnet-4-20250514"` model indicating fallback.

## Cleanup

1. Stop the running NGINX AI proxy Docker container. It should automatically get deleted from your container cache:

    ```bash
    docker stop nginx-ai-proxy
    ```

2. Cleanup the Docker key volume we created in one of the first steps:

    ```bash
    docker volume rm nginx-keys
    ```

## Demo Structure

### Files

| Path | Purpose |
|------|---------|
| [`config/nginx.conf`](config/nginx.conf) | Includes the default `nginx.conf` file with a few modifications. Major differences are loading the NJS module, tweaking the log format to include token vars and "including" the AI proxy NGINX config (`aiproxy.conf`) |
| [`config/aiproxy.conf`](config/aiproxy.conf) | Includes upstream blocks for OpenAI/Anthropic with dynamic DNS resolution, sets up a server listening on port 4242, loads a JSON config into the `$ai_proxy_config` variable using NJS, exposes a `/v1/chat/completions` location entrypoint, and setups internal locations for the `/openai` and `/anthropic` models |
| [`config/rbac.json`](config/rbac.json) | Includes the RBAC data in a JSON data format -- See section below for more information |
| [`njs/aiproxy.js`](njs/aiproxy.js) | NJS script including JSON RBAC parsing and AI proxy routing logic (authorization, model lookup, model failover, provider-specific transforms, and token extraction) |
| [`templates/*.template`](templates/) | `envsubst` templates to inject API keys into included snippets |

### RBAC JSON Configuration Model

The [JSON RBAC model](config/rbac.json) looks like this:

```json
{
  "users": {
    "user-a": {
      "models": [
        {"name": "gpt-5", "failover": "claude-sonnet-4-20250514"},
        {"name": "claude-sonnet-4-20250514"}
      ]
    },
    "user-b": {
      "models": [{"name": "gpt-5"}]
    }
  },
  "models": {
    "gpt-5": {"provider": "openai", "location": "/openai"},
    "claude-sonnet-4-20250514": {"provider": "anthropic", "location": "/anthropic"}
  }
}
```

Each user contains a list of allowed models (and an optional `failover` model). The model section maps logical model names to a provider name and the internal location used by NGINX.

### NGINX Request Processing Flow

1. A client POSTs an OpenAI chat completion request containing the appropriate JSON data to `/v1/chat/completions`. The header `X-User` details which user this client corresponds to.
2. The `aiproxy.js` NJS script validates the user and model access.
3. NGINX proxies the request to the appropriate model via an internal location block (`/openai` or `/anthropic`).
4. If the provider is Anthropic, the request is transformed by the NJS script to an Anthropic API compatible request. The response is then transformed back to an OpenAI compatible response.
5. If the primary model returns a non-200 status code and a `failover` model is defined, a second attempt is made to the `failover` model.
6. Once a successful request is completed, token counts are extracted from the response and logged within the NGINX access log.

### Token Usage Logging in NGINX

Token usage data is saved into NGINX variables using NJS. These variables, `$ai_proxy_response_prompt_tokens`, `$ai_proxy_response_completion_tokens`, and `$ai_proxy_response_total_tokens`, are then included into the access log format in the core NGINX config file (`nginx.conf`). Failed requests produce empty values. The resulting access log could look something along these lines:

```console
... 401 ... prompt_tokens= completion_tokens= total_tokens=
... 200 ... prompt_tokens=13 completion_tokens=39 total_tokens=52
```
