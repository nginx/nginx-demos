# NGINX API Gateway Demo

## Overview

This demo uses Terraform to automate the setup of an NGINX API gateway pseudo-production environment that includes a mock API backend database.

## Requirements

### Terraform

This demo has been developed and tested with Terraform `0.13` through `1.1.5`.

Instructions on how to install Terraform can be found in the [Terraform website](https://www.terraform.io/downloads.html).

### AWS R53

You will need to create R53 hosted zone beforehand. Make sure you own the domain you are using through the R53 hosted zone or you risk running into DNS issues. You should specify the R53 hosted zone `id` as well as a FQDN for the NGINX Plus API gateway and backend API in the corresponding Terraform variables.

## Deployment

To use the provided Terraform scripts, you need to:

1. Export your AWS credentials as environment variables (or alternatively, tweak the AWS provider in [`terraform/provider.tf`](terraform/provider.tf)).
2. Set up default values for variables missing a value in [`terraform/variables.tf`](terraform/variables.tf) (you can find example values commented out in the file). Alternatively, you can input those variables at runtime (beware of dictionary values if you do the latter).

Once you have configured your Terraform environment, you can either:

* Run [`./setup.sh`](setup.sh) to initialize the AWS Terraform provider and start a Terraform deployment on AWS.
* Run `terraform init` and `terraform apply`.

And finally, once you are done playing with the demo, you can destroy the AWS infrastructure by either:

* Run [`./cleanup.sh`](cleanup.sh) to destroy your Terraform deployment.
* Run `terraform destroy`.

## Demo Overview

You will find a series of NGINX configuration files in the [`nginx_api_gateway_config`](nginx_api_gateway_config/) folder. The folder is divided into individual steps, meant to be copied into their respective directory in order. By default, the folder is uploaded to your NGINX API gateway instance.

Do note that you will have to replace the `<backend-api-fqdn>` placeholder value found in the API backends NGINX configuration file in Step 3 with the corresponding value you used when deploying the Terraform environment (see [`nginx_api_gateway_config/step_3/api_backends.conf`](nginx_api_gateway_config/step_3/api_backends.conf) for more details).

A deployment script to help you copy the configuration files, [`deploy.sh`](nginx_api_gateway_config/deploy.sh), is also provided. To run the script, use the step number as a parameter, e.g. `./deploy.sh 1` for step 1. You might need to make the deployment script executable by running `sudo chmod +x deploy.sh`.

### Step 1 -> Define the entry point of the NGINX API gateway

To deploy:

`./deploy.sh 1`

To test:

`curl -s http://localhost:8080`

Expected response:

```html
<html>
<head><title>400 Bad Request</title></head>
<body>
<center><h1>400 Bad Request</h1></center>
<hr><center>nginx/1.19.5</center>
</body>
</html>
```

### Step 2 -> Define default JSON error codes

To deploy:

`./deploy.sh 2`

To test:

`curl -s http://localhost:8080 | jq`

Expected response:

```json
{"status":400,"message":"Bad request"}
```

To test (headers):

`curl -sI http://localhost:8080`

Expected response:

```text
HTTP/1.1 400 Bad Request
...
```

### Step 3 -> Define the API endpoints and upstream/backend servers

To deploy:

`./deploy.sh 3`

To test:

`curl -s http://localhost:8080/api/f1/drivers/alonso | jq`

Expected response:

```json
{
  "MRData": {
    "xmlns": "http://ergast.com/mrd/1.5",
    "series": "f1",
    "url": "http://ergast.com/api/f1/drivers/alonso",
    "limit": "30",
    "offset": "0",
    "total": "1",
    "DriverTable": {
      "driverId": "alonso",
      "Drivers": [
        {
          "driverId": "alonso",
          "permanentNumber": "14",
          "code": "ALO",
          "url": "http://en.wikipedia.org/wiki/Fernando_Alonso",
          "givenName": "Fernando",
          "familyName": "Alonso",
          "dateOfBirth": "1981-07-29",
          "nationality": "Spanish"
        }
      ]
    }
  }
}
```

### Step 4 -> Enable rate limiting

To deploy:

`./deploy.sh 4`

To test (run multiple times in quick succession):

`curl -s http://localhost:8080/api/f1/drivers/alonso | jq`

Expected response:

```json
{"status":429,"message":"API rate limit exceeded"}
```

### Step 5 -> Set up API Key authentication

To deploy:

`./deploy.sh 5`

To test (unauthorized requests):

`curl -s http://localhost:8080/api/f1/drivers/alonso | jq`

Expected response (unauthorized requests):

```json
{"status":401,"message":"Unauthorized"}
```

To test (authorized requests):

`curl -sH "apikey: 7B5zIqmRGXmrJTFmKa99vcit" http://localhost:8080/api/f1/drivers/alonso | jq`

Expected response (authorized requests):

```json
{"MRData": {
    "xmlns": "http://ergast.com/mrd/1.4",
    "series": "f1",
    "url": "http://ergast.com/api/f1/drivers/alonso",
    ...
}}
```

### Step 6 -> Set up JSON body validation using NJS

To deploy:

`./deploy.sh 6`

To test (incorrect JSON):

`curl -sH "apikey: 7B5zIqmRGXmrJTFmKa99vcit" -i -X POST -d 'garbage123' http://localhost:8080/api/f1/seasons`

Expected response (incorrect JSON):

```text
HTTP/1.1 415 Unsupported Media Type
```

To test (correct JSON):

`curl -sH "apikey: 7B5zIqmRGXmrJTFmKa99vcit" -i -X POST -d '{"season":"2020"}' http://localhost:8080/api/f1/seasons | jq`

Expected response (correct JSON):

```text
HTTP/1.1 200 OK
```
