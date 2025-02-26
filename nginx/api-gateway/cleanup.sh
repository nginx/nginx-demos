cd terraform \
&& terraform destroy -auto-approve \
&& rm -f terraform.tfstate terraform.tfstate.backup \
&& rm -rf .terraform
