# NGINX Ingress Controller Demo

## Prerequisites

### Install VSCode

1. Go to the [VSCode download page](https://code.visualstudio.com/download).
2. Download the installer for your operating system.
3. Run the installer and follow the on-screen instructions to complete the installation.

### Install Git

1. Open a terminal or command prompt.
2. Run the following command to install Git using Winget:
    ```shell
    winget install -e --id Git.Git
    ```

### Authenticate with GitHub

1. Open a terminal or command prompt.
2. Configure your GitHub email:
    ```shell
    git config --global user.email "you@example.com"
    ```
3. Configure your GitHub username:
    ```shell
    git config --global user.name "Your Name"
    ```

### Install Docker Desktop

1. Open a terminal or command prompt.
2. Run the following command to install Docker Desktop using Winget:
    ```shell
    winget install -e --id Docker.DockerDesktop
    ```
3. Follow the on-screen instructions to complete the installation.

### Install Helm

1. Open a terminal or command prompt.
2. Run the following command to install Helm using Winget:
    ```shell
    winget install -e --id Helm.Helm
    ```
3. Follow the on-screen instructions to complete the installation.

## Demo Commands

### Add NGINX Helm Repository

1. Open a terminal or command prompt.
2. Run the following command to add the NGINX stable Helm repository:
    ```bash
    helm repo add nginx-stable https://helm.nginx.com/stable
    ```

### Update Helm Repositories

1. Open a terminal or command prompt.
2. Run the following command to update your Helm repositories:
    ```bash
    helm repo update
    ```

### Apply Kubernetes CRDs

1. Open a terminal or command prompt.
2. Run the following command to apply the NGINX Kubernetes CRDs:
    ```bash
    kubectl apply -f https://raw.githubusercontent.com/nginx/kubernetes-ingress/v4.0.1/deploy/crds.yaml
    ```

### Install NGINX Ingress Controller

1. Open a terminal or command prompt.
2. Run the following command to install the NGINX Ingress Controller using Helm:
    ```bash
    helm install nginx-ingress nginx-stable/nginx-ingress --namespace default
    ```

### Deploy NGINX Application

1. Open a terminal or command prompt.
2. Run the following command to apply the NGINX deployment configuration:
    ```bash
    kubectl apply -f nginx-deployment.yaml
    ```

### Apply NGINX Ingress Configuration

1. Open a terminal or command prompt.
2. Run the following command to apply the NGINX ingress configuration:
    ```bash
    kubectl apply -f nginx-ingress.yaml
    ```

## Demo Take Aways

- Intalling NGINX Ingress Controller (NIC) is quick and simple to get up and functional in Kubernetes (K8s)
- NGINX web server and NIC on K8s on Docker Desktop provides a light weight low stress K8s dev environment to experiment and lean.
- Recovery from issues is as simple as performing a quick reset of K8s and a few minutes to stand up the envionment again.
- Join the community and become a contributor.