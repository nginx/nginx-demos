import fs from 'fs';

// Loads RBAC configuration from a JSON file and sets it to an NGINX variable
function load_rbac() {
    try {
        // Adjust the path as needed
        let config = fs.readFileSync('/etc/nginx/rbac.json', 'utf8');
        return config;
    } catch (e) {
        return JSON.stringify({
            error: "Failed to load RBAC: " + e.message
        });
    }
}

// Convert an OpenAI compatible request to Anthropic's request format
function transformAnthropicRequest(requestBody) {
    // Anthropic requires max_tokens, but our API may not always specify it -> fallback to defaults if not provided
    let maxTokens = requestBody.max_completion_tokens || requestBody.max_tokens || 512;

    const anthropicRequest = {
        model: requestBody.model,
        max_tokens: maxTokens,
        stream: requestBody.stream || false,
        temperature: requestBody.temperature || 1.0,
        top_p: requestBody.top_p
    };

    // Scale Anthropic temperature based on its acceptable range (0-1) vs OpenAI (0-2)
    if (anthropicRequest.temperature > 1.0) {
        anthropicRequest.temperature = requestBody.temperature / 2.0;
    }

    // Convert stop sequences to Anthropic's format
    if (requestBody.stop) {
        anthropicRequest.stop_sequences = Array.isArray(requestBody.stop) ? requestBody.stop : [requestBody.stop];
    }

    // Separate system messages from user/assistant messages
    const systemMessages = [];
    const messages = [];

    for (let i = 0; i < requestBody.messages.length; i++) {
        const msg = requestBody.messages[i];
        if (msg.role === "system") {
            systemMessages.push({text: msg.content, type: "text"});
        } else {
            messages.push({role: msg.role, content: msg.content});
        }
    }

    // Attach system messages if present
    if (systemMessages.length > 0) {
        anthropicRequest.system = systemMessages;
    }
    anthropicRequest.messages = messages;

    return anthropicRequest;
}

// Convert an Anthropic response to an OpenAI response format
function transformAnthropicResponse(anthropicResponse) {
    const response = JSON.parse(anthropicResponse);

    // Handle error responses from Anthropic
    if (response.error) {
        return {
            error: {
                type: response.error.type,
                message: response.error.message,
                code: response.error.code
            }
        };
    }

    // Map Anthropic's successful response to OpenAI's expected structure
    const openaiResponse = {
        id: response.id,
        object: "chat.completion", // Standardize object type
        model: response.model,
        choices: [],
        usage: {
            prompt_tokens: response.usage.input_tokens,
            completion_tokens: response.usage.output_tokens,
            total_tokens: response.usage.input_tokens + response.usage.output_tokens
        }
    };

    // Convert content to choices format
    for (let i = 0; i < response.content.length; i++) {
        const content = response.content[i];
        openaiResponse.choices.push({
            index: i,
            finish_reason: response.stop_reason,
            message: {
                role: response.role,
                content: content.text
            }
        });
    }

    return openaiResponse;
}

// Attempts to call the specified model provider (Anthropic or OpenAI)
// Transforms the request as needed and issues a subrequest to the provider's location
async function tryModel(r, modelConfig, requestBody) {
    const location = modelConfig.location;
    let subrequestBody;

    // Transform request body for Anthropic, or pass through for OpenAI
    if (modelConfig.provider === "anthropic") {
        const transformedRequest = transformAnthropicRequest(requestBody);
        subrequestBody = JSON.stringify(transformedRequest);
    } else if (modelConfig.provider === "openai") {
        // For OpenAI, pass the request as-is (no transformation needed)
        subrequestBody = JSON.stringify(requestBody);
    } else {
        throw new Error(`Provider '${modelConfig.provider}' not supported`);
    }

    // Issue subrequest to the model provider
    return await r.subrequest(location, {
        method: 'POST',
        body: subrequestBody
    });
}

// Returns the response body in the correct format for the client
// Transforms Anthropic responses to OpenAI format, passes OpenAI through
function getResponseBody(modelConfig, serviceReply) {
    if (modelConfig.provider === "anthropic") {
        const transformedResponse = transformAnthropicResponse(serviceReply.responseText);
        return JSON.stringify(transformedResponse);
    } else {
        return serviceReply.responseText; // Pass through as-is for OpenAI
    }
}

// Main routing function for the AI proxy
// Handles user authentication, model selection, failover, and response transformation
async function route(r) {
    try {
        // Parse the AI proxy configuration from NGINX variable
        const configStr = r.variables.ai_proxy_config;
        if (!configStr) {
            r.return(500, JSON.stringify({
                error: {
                    message: "AI proxy configuration was not found"
                }
            }));
            return;
        }

        // Parse the configuration JSON
        let config;
        try {
            config = JSON.parse(configStr);
        } catch (e) {
            r.return(500, JSON.stringify({
                error: {
                    message: "Invalid AI proxy configuration JSON"
                }
            }));
            return;
        }

        // Extract the user from NGINX variable (set by header)
        const user = r.variables.aiproxy_user;
        if (!user) {
            r.return(401, JSON.stringify({
                error: {
                    message: "User not specified"
                }
            }));
            return;
        }

        // Check if user exists in configuration
        if (!config.users || !config.users[user]) {
            r.return(403, JSON.stringify({
                error: {
                    message: "User not authorized"
                }
            }));
            return;
        }

        // Check the JSON validity of the AI proxy request body
        let requestBody;
        try {
            requestBody = JSON.parse(r.requestText);
        } catch (e) {
            r.return(400, JSON.stringify({
                error: {
                    message: "Invalid JSON in request body"
                }
            }));
            return;
        }

        // Extract the model from the request
        const requestedModel = requestBody.model;
        if (!requestedModel) {
            r.return(400, JSON.stringify({
                error: {
                    message: "Model not specified in request"
                }
            }));
            return;
        }

        // Check if the requested model is available to the user
        const userModels = config.users[user].models;
        const userModel = userModels.find(m => m.name === requestedModel);

        if (!userModel) {
            r.return(404, JSON.stringify({
                error: {
                    message: `The model '${requestedModel}' was not found or is not accessible to this user`
                }
            }));
            return;
        }

        // Get the model configuration from the global config
        const modelConfig = config.models[requestedModel];
        if (!modelConfig) {
            r.return(500, JSON.stringify({
                error: {
                    message: `Model '${requestedModel}' configuration not found`
                }
            }));
            return;
        }

        // Try primary model first
        let serviceReply = await tryModel(r, modelConfig, requestBody);
        let usedModelConfig = modelConfig;

        // If primary model failed (status code is not 200) and failover is configured, try failover
        if (serviceReply.status !== 200 && userModel.failover) {
            r.log(`Primary model '${requestedModel}' failed with status ${serviceReply.status}, trying failover model '${userModel.failover}'`);

            // Get failover model configuration
            const failoverModelConfig = config.models[userModel.failover];
            if (!failoverModelConfig) {
                r.error(`Failover model '${userModel.failover}' configuration not found`);
                // Return the original error since failover is misconfigured
                let responseBody = getResponseBody(modelConfig, serviceReply);
                r.return(serviceReply.status, responseBody);
                return;
            }

            // Update the request body to use the failover model
            const failoverRequestBody = Object.assign({}, requestBody, {model: userModel.failover});

            // Try the failover model
            serviceReply = await tryModel(r, failoverModelConfig, failoverRequestBody);
            usedModelConfig = failoverModelConfig;
        }

        // Transform and return response body based on provider that was actually used
        let responseBody = getResponseBody(usedModelConfig, serviceReply);

        // Extract token usage information from response and set NGINX variables for logging
        if (serviceReply.status === 200) {
            try {
                const parsedResponse = JSON.parse(responseBody);
                if (parsedResponse.usage) {
                    r.variables.ai_proxy_response_prompt_tokens = parsedResponse.usage.prompt_tokens || "";
                    r.variables.ai_proxy_response_completion_tokens = parsedResponse.usage.completion_tokens || "";
                    r.variables.ai_proxy_response_total_tokens = parsedResponse.usage.total_tokens || "";
                }
            } catch (e) {
                r.log(`Warning: Failed to parse response body for token extraction: ${e.toString()}`);
            }
        }

        r.return(serviceReply.status, responseBody);

    } catch (e) {
        r.log(`Error: ${e.toString()}`);
        r.return(500, JSON.stringify({
            error: {
                message: "Internal server error",
            }
        }));
    }
}

// Export the main handlers for use in NGINX config
export default { load_rbac, route};
