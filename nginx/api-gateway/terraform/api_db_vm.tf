# Mock NGINX Plus Sports API containing mock Baseball, Golf, and Tennis data
# Also launches an Ergast F1 API container
resource "aws_instance" "backend_api" {
  ami           = data.aws_ami.ubuntu.id
  instance_type = var.backend_api_machine_type
  key_name      = var.key_data["name"]
  vpc_security_group_ids = [
    aws_security_group.backend_api.id,
  ]
  subnet_id = aws_subnet.main.id
  user_data = <<EOF
#!/bin/sh
set -ex
apt update
apt install -y \
    apt-transport-https \
    ca-certificates \
    curl \
    git \
    gnupg-agent \
    jq \
    software-properties-common
curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo apt-key add -
add-apt-repository \
    "deb [arch=amd64] https://download.docker.com/linux/ubuntu \
    $(lsb_release -cs) \
    stable"
apt update
install -m 0755 -d /etc/apt/keyrings
curl -fsSL https://download.docker.com/linux/ubuntu/gpg -o /etc/apt/keyrings/docker.asc
chmod a+r /etc/apt/keyrings/docker.asc
echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.asc] https://download.docker.com/linux/ubuntu $(. /etc/os-release && echo "$${UBUNTU_CODENAME:-$VERSION_CODENAME}") stable" | tee /etc/apt/sources.list.d/docker.list > /dev/null
apt install -y docker-ce docker-ce-cli docker-compose-plugin containerd.io
git clone https://github.com/lcrilly/ergast-f1-api /home/ubuntu/ergast-f1-api
# sed -i -e 's/FROM mysql/FROM mysql:5.6/g' /home/ubuntu/ergast-f1-api/ergastdb/Dockerfile
sed -i 's/$format = "xml";/$format = "json";/' /home/ubuntu/ergast-f1-api/webroot/php/api/index.php
docker compose -f /home/ubuntu/ergast-f1-api/docker-compose.yaml up --build  -d --remove-orphans

apt update
apt install -y ca-certificates curl gnupg2 lsb-release ubuntu-keyring
curl https://nginx.org/keys/nginx_signing.key | gpg --dearmor | tee /usr/share/keyrings/nginx-archive-keyring.gpg >/dev/null
gpg --dry-run --quiet --no-keyring --import --import-options import-show /usr/share/keyrings/nginx-archive-keyring.gpg
echo "deb [signed-by=/usr/share/keyrings/nginx-archive-keyring.gpg] https://nginx.org/packages/mainline/ubuntu `lsb_release -cs` nginx" | tee /etc/apt/sources.list.d/nginx.list
printf "Package: *\nPin: origin nginx.org\nPin: release o=nginx\nPin-Priority: 900\n" | tee /etc/apt/preferences.d/99nginx
apt update
apt install -y nginx nginx-module-njs
sed -i '1iload_module modules/ngx_http_js_module.so;' /etc/nginx/nginx.conf
cat > /etc/nginx/conf.d/default.conf <<'EOL'
${templatefile("backend_api/default.conf", {
  fqdn = 0,
})}
EOL
cat > /etc/nginx/conf.d/echo.js <<EOL
${file("backend_api/echo.js")}
EOL
service nginx start
EOF
  tags = {
    Name  = "backend_api",
    Owner = var.owner,
    user  = "ubuntu",
  }
}

# Enable SSL on backend API
resource "null_resource" "tweak_backend_api_config" {
  count = var.backend_api_certbot ? 1 : 0
  provisioner "file" {
    content = templatefile("backend_api/default.conf", {
      fqdn = aws_route53_record.backend_api.fqdn
    })
    destination = "/home/ubuntu/default.conf"
  }
  provisioner "remote-exec" {
    inline = [
      "set -ex",
      "while [ ! -f /etc/nginx/conf.d/echo.js ]; do sleep 5; done",
      "sudo snap install core",
      "sudo snap refresh core",
      "sudo snap install --classic certbot",
      "sudo ln -s /certbot /usr/bin/certbot",
      "sudo certbot certonly --nginx --agree-tos --register-unsafely-without-email -d ${aws_route53_record.backend_api.fqdn}",
      "sudo mv /home/ubuntu/default.conf /etc/nginx/conf.d/default.conf",
      "sudo nginx -s reload",
    ]
  }
  connection {
    type        = "ssh"
    user        = "ubuntu"
    private_key = file(var.key_data["location"])
    host        = aws_route53_record.backend_api.fqdn
  }
  depends_on = [
    aws_instance.backend_api,
  ]
}
