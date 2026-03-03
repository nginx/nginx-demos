{{/*
Expand the name of the chart.
*/}}
{{- define "nginx-plus.name" -}}
{{- default .Chart.Name .Values.nameOverride | trunc 63 | trimSuffix "-" }}
{{- end }}

{{/*
Create a default fully qualified app name.
*/}}
{{- define "nginx-plus.fullname" -}}
{{- if .Values.fullnameOverride }}
{{- .Values.fullnameOverride | trunc 63 | trimSuffix "-" }}
{{- else }}
{{- $name := default .Chart.Name .Values.nameOverride }}
{{- if contains $name .Release.Name }}
{{- .Release.Name | trunc 63 | trimSuffix "-" }}
{{- else }}
{{- printf "%s-%s" .Release.Name $name | trunc 63 | trimSuffix "-" }}
{{- end }}
{{- end }}
{{- end }}

{{/*
Chart label.
*/}}
{{- define "nginx-plus.chart" -}}
{{- printf "%s-%s" .Chart.Name .Chart.Version | replace "+" "_" | trunc 63 | trimSuffix "-" }}
{{- end }}

{{/*
Common labels applied to every resource.
*/}}
{{- define "nginx-plus.labels" -}}
helm.sh/chart: {{ include "nginx-plus.chart" . }}
{{ include "nginx-plus.selectorLabels" . }}
{{- if .Chart.AppVersion }}
app.kubernetes.io/version: {{ .Chart.AppVersion | quote }}
{{- end }}
app.kubernetes.io/managed-by: {{ .Release.Service }}
{{- with .Values.commonLabels }}
{{ toYaml . }}
{{- end }}
{{- end }}

{{/*
Selector labels — stable across upgrades.
*/}}
{{- define "nginx-plus.selectorLabels" -}}
app.kubernetes.io/name: {{ include "nginx-plus.name" . }}
app.kubernetes.io/instance: {{ .Release.Name }}
{{- end }}

{{/*
ServiceAccount name.
*/}}
{{- define "nginx-plus.serviceAccountName" -}}
{{- if .Values.serviceAccount.create }}
{{- default (include "nginx-plus.fullname" .) .Values.serviceAccount.name }}
{{- else }}
{{- default "default" .Values.serviceAccount.name }}
{{- end }}
{{- end }}

{{/*
Environment variables — exactly the env vars present in the nginx-demos manifest.
*/}}
{{- define "nginx-plus.envVars" -}}

{{- /* ---- NGINX_LICENSE (JWT, from Secret) ---- */}}
- name: NGINX_LICENSE
  valueFrom:
    secretKeyRef:
      name: {{ .Values.license.secretName | quote }}
      key: {{ .Values.license.secretKey | quote }}

{{- /* ---- NGINX Agent ---- */}}
- name: NGINX_AGENT_ENABLED
  value: {{ .Values.agent.enabled | ternary "true" "false" | quote }}

{{- if .Values.agent.enabled }}
- name: NGINX_AGENT_SERVER_HOST
  value: {{ required "agent.serverHost is required when agent.enabled=true" .Values.agent.serverHost | quote }}
- name: NGINX_AGENT_SERVER_GRPCPORT
  value: {{ .Values.agent.serverGrpcPort | quote }}
- name: NGINX_AGENT_TLS_ENABLE
  value: {{ .Values.agent.tlsEnable | quote }}
- name: NGINX_AGENT_TLS_SKIP_VERIFY
  value: {{ .Values.agent.tlsSkipVerify | quote }}
{{- if .Values.agent.serverToken }}
- name: NGINX_AGENT_SERVER_TOKEN
  value: {{ .Values.agent.serverToken | quote }}
{{- end }}
{{- if .Values.agent.instanceGroup }}
- name: NGINX_AGENT_INSTANCE_GROUP
  value: {{ .Values.agent.instanceGroup | quote }}
{{- end }}
{{- if .Values.agent.tags }}
- name: NGINX_AGENT_TAGS
  value: {{ .Values.agent.tags | quote }}
{{- end }}
- name: NGINX_AGENT_LOG_LEVEL
  value: {{ .Values.agent.logLevel | quote }}
- name: NGINX_AGENT_ALLOWED_DIRECTORIES
  value: {{ .Values.agent.allowedDirectories | quote }}
{{- if .Values.agent.features }}
- name: NGINX_AGENT_FEATURES
  value: {{ .Values.agent.features | quote }}
{{- end }}
{{- end }}

{{- /* ---- NAP WAF ---- */}}
{{- if .Values.waf.enabled }}
- name: NAP_WAF
  value: "true"
- name: NAP_WAF_PRECOMPILED_POLICIES
  value: {{ .Values.waf.precompiledPolicies | ternary "true" "false" | quote }}
{{- end }}

{{- /* ---- Extra arbitrary env vars ---- */}}
{{- with .Values.extraEnv }}
{{ toYaml . }}
{{- end }}
{{- end }}


