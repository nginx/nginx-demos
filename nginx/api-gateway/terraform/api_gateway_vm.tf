# NGINX API Gateway instance
resource "aws_instance" "nginx_api_gateway" {
  ami           = data.aws_ami.ubuntu.id
  instance_type = var.nginx_api_gateway_machine_type
  key_name      = var.key_data["name"]
  vpc_security_group_ids = [
    aws_security_group.nginx_api_gateway.id,
  ]
  subnet_id = aws_subnet.main.id
  user_data = <<EOF
#!/bin/sh
set -ex
apt update
apt install -y ca-certificates curl gnupg2 lsb-release ubuntu-keyring
curl https://nginx.org/keys/nginx_signing.key | gpg --dearmor | tee /usr/share/keyrings/nginx-archive-keyring.gpg >/dev/null
gpg --dry-run --quiet --no-keyring --import --import-options import-show /usr/share/keyrings/nginx-archive-keyring.gpg
echo "deb [signed-by=/usr/share/keyrings/nginx-archive-keyring.gpg] https://nginx.org/packages/mainline/ubuntu `lsb_release -cs` nginx" | tee /etc/apt/sources.list.d/nginx.list
printf "Package: *\nPin: origin nginx.org\nPin: release o=nginx\nPin-Priority: 900\n" | tee /etc/apt/preferences.d/99nginx
apt update
apt install -y nginx nginx-module-njs jq
service nginx start
EOF
  tags = {
    Name  = "nginx_api_gateway",
    Owner = var.owner,
    user  = "ubuntu",
  }
}

# Enable SSL on NGINX API gateway
resource "null_resource" "tweak_nginx_api_gateway_config" {
  count = var.nginx_api_gateway_certbot ? 1 : 0
  provisioner "remote-exec" {
    inline = [
      "set -ex",
      "while [ ! -f /etc/nginx/nginx.conf ]; do sleep 5; done",
      "sudo snap install core",
      "sudo snap refresh core",
      "sudo snap install --classic certbot",
      "sudo ln -s /certbot /usr/bin/certbot",
      "sudo certbot --nginx --redirect --agree-tos --register-unsafely-without-email -d ${aws_route53_record.nginx_api_gateway.fqdn}",
    ]
  }
  connection {
    type        = "ssh"
    user        = "ubuntu"
    private_key = file(var.key_data["location"])
    host        = aws_route53_record.nginx_api_gateway.fqdn
  }
  depends_on = [
    aws_instance.nginx_api_gateway,
  ]
}

# Upload NGINX configuration files to NGINX API gateway
resource "null_resource" "upload_nginx_api_gateway_config_files" {
  count = var.upload_nginx_api_gateway_config_files ? 1 : 0
  provisioner "remote-exec" {
    inline = [
      "set -ex",
      "while [ ! -f /etc/nginx/nginx.conf ]; do sleep 5; done",
    ]
  }
  provisioner "file" {
    source      = "../nginx_api_gateway_config"
    destination = "/home/ubuntu"
  }
  connection {
    type        = "ssh"
    user        = "ubuntu"
    private_key = file(var.key_data["location"])
    host        = aws_route53_record.nginx_api_gateway.fqdn
  }
  depends_on = [
    aws_instance.nginx_api_gateway,
  ]
}
