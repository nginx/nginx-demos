# Create Elastic IP for the NGINX API gateway
resource "aws_eip" "nginx_api_gateway" {
  instance = aws_instance.nginx_api_gateway.id
  domain   = "vpc"
  tags = {
    Name  = "nginx_api_gateway",
    Owner = var.owner,
  }
}

# Create Elastic IP for the backend API
resource "aws_eip" "backend_api" {
  instance = aws_instance.backend_api.id
  domain   = "vpc"
  tags = {
    Name  = "backend_api",
    Owner = var.owner,
  }
}

# Create an A record for the NGINX API gateway EIP
resource "aws_route53_record" "nginx_api_gateway" {
  zone_id = var.r53_zone
  name    = var.nginx_api_gateway_fqdn
  type    = "A"
  ttl     = "300"
  records = [
    aws_eip.nginx_api_gateway.public_ip
  ]
}

# Create an A record for the backend API EIP
resource "aws_route53_record" "backend_api" {
  zone_id = var.r53_zone
  name    = var.backend_api_fqdn
  type    = "A"
  ttl     = "300"
  records = [
    aws_eip.backend_api.public_ip
  ]
}
