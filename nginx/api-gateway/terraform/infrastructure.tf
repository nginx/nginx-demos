# Create AWS main VPC
resource "aws_vpc" "main" {
  cidr_block           = "10.0.0.0/16"
  enable_dns_hostnames = true
  tags = {
    Name  = "nginx_api_gateway_vpc"
    Owner = var.owner,
  }
}

# Create AWS main subnet
resource "aws_subnet" "main" {
  vpc_id                  = aws_vpc.main.id
  cidr_block              = "10.0.0.0/24"
  map_public_ip_on_launch = true
  tags = {
    Name  = "nginx_api_gateway_subnet"
    Owner = var.owner,
  }
}

# Create AWS security group for NGINX API gateway instances
resource "aws_security_group" "nginx_api_gateway" {
  name        = "nginx-api-gateway-sg"
  description = "Security group for NGINX API gateway"
  vpc_id      = aws_vpc.main.id
  tags = {
    Name  = "nginx_api_gateway_sg"
    Owner = var.owner,
  }
}

resource "aws_security_group_rule" "nginx_api_gateway_ingress_ssh" {
  type              = "ingress"
  from_port         = 22
  to_port           = 22
  protocol          = "tcp"
  security_group_id = aws_security_group.nginx_api_gateway.id
  cidr_blocks = [
    "0.0.0.0/0",
  ]
}

resource "aws_security_group_rule" "nginx_api_gateway_ingress_http" {
  type              = "ingress"
  from_port         = 80
  to_port           = 80
  protocol          = "tcp"
  security_group_id = aws_security_group.nginx_api_gateway.id
  cidr_blocks = [
    "0.0.0.0/0",
  ]
}

resource "aws_security_group_rule" "nginx_api_gateway_ingress_https" {
  type              = "ingress"
  from_port         = 443
  to_port           = 443
  protocol          = "tcp"
  security_group_id = aws_security_group.nginx_api_gateway.id
  cidr_blocks = [
    "0.0.0.0/0",
  ]
}

resource "aws_security_group_rule" "nginx_api_gateway_ingress_api" {
  type              = "ingress"
  from_port         = 8080
  to_port           = 8080
  protocol          = "tcp"
  security_group_id = aws_security_group.nginx_api_gateway.id
  cidr_blocks = [
    "0.0.0.0/0",
  ]
}

resource "aws_security_group_rule" "nginx_api_gateway_egress_http" {
  type              = "egress"
  from_port         = 80
  to_port           = 80
  protocol          = "tcp"
  security_group_id = aws_security_group.nginx_api_gateway.id
  cidr_blocks = [
    "0.0.0.0/0",
  ]
}

resource "aws_security_group_rule" "nginx_api_gateway_egress_https" {
  type              = "egress"
  from_port         = 443
  to_port           = 443
  protocol          = "tcp"
  security_group_id = aws_security_group.nginx_api_gateway.id
  cidr_blocks = [
    "0.0.0.0/0",
  ]
}

resource "aws_security_group_rule" "nginx_api_gateway_egress_f1" {
  type              = "egress"
  from_port         = 8000
  to_port           = 8000
  protocol          = "tcp"
  security_group_id = aws_security_group.nginx_api_gateway.id
  cidr_blocks = [
    "0.0.0.0/0",
  ]
}

resource "aws_security_group" "backend_api" {
  name        = "backend-api-sg"
  description = "Security group for backend API"
  vpc_id      = aws_vpc.main.id
  tags = {
    Owner = var.owner,
    Name  = "backend_api_sg"
  }
}

resource "aws_security_group_rule" "backend_api_ingress_ssh" {
  type              = "ingress"
  from_port         = 22
  to_port           = 22
  protocol          = "tcp"
  security_group_id = aws_security_group.backend_api.id
  cidr_blocks = [
    "0.0.0.0/0",
  ]
}

resource "aws_security_group_rule" "backend_api_ingress_http" {
  type              = "ingress"
  from_port         = 80
  to_port           = 80
  protocol          = "tcp"
  security_group_id = aws_security_group.backend_api.id
  cidr_blocks = [
    "0.0.0.0/0",
  ]
}

resource "aws_security_group_rule" "backend_api_ingress_https" {
  type              = "ingress"
  from_port         = 443
  to_port           = 443
  protocol          = "tcp"
  security_group_id = aws_security_group.backend_api.id
  cidr_blocks = [
    "0.0.0.0/0",
  ]
}

resource "aws_security_group_rule" "backend_api_ingress_ergast" {
  type              = "ingress"
  from_port         = 8000
  to_port           = 8000
  protocol          = "tcp"
  security_group_id = aws_security_group.backend_api.id
  cidr_blocks = [
    "0.0.0.0/0",
  ]
}

resource "aws_security_group_rule" "backend_api_egress_http" {
  type              = "egress"
  from_port         = 80
  to_port           = 80
  protocol          = "tcp"
  security_group_id = aws_security_group.backend_api.id
  cidr_blocks = [
    "0.0.0.0/0",
  ]
}

resource "aws_security_group_rule" "backend_api_egress_https" {
  type              = "egress"
  from_port         = 443
  to_port           = 443
  protocol          = "tcp"
  security_group_id = aws_security_group.backend_api.id
  cidr_blocks = [
    "0.0.0.0/0",
  ]
}

# Create AWS internet gateway
resource "aws_internet_gateway" "main" {
  vpc_id = aws_vpc.main.id
  tags = {
    Owner = var.owner,
    Name  = "nginx_api_gateway_gtwy"
  }
}

# Configure AWS network route
resource "aws_route" "main" {
  route_table_id         = aws_vpc.main.main_route_table_id
  destination_cidr_block = "0.0.0.0/0"
  gateway_id             = aws_internet_gateway.main.id
}
