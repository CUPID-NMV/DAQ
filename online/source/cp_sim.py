#!/usr/bin/env python3
import os
import sys
import json
import boto3
import requests

S3_ENDPOINT = "https://s3.cr.cnaf.infn.it:7480"
ROLE_ARN = "arn:aws:iam::cygno:role/IAMaccess"
REGION = "oidc"
BUCKET = "cygno-data"
KEY_PREFIX = "NMV/WC/SIM"

REFRESH_TOKEN = os.environ["REFRESH_TOKEN"]
IAM_CLIENT_SECRET = os.environ["IAM_CLIENT_SECRET"]
IAM_CLIENT_ID = os.environ["IAM_CLIENT_ID"]
IAM_SERVER = os.environ["IAM_SERVER"].rstrip("/")


def get_access_token():
    resp = requests.post(
        f"{IAM_SERVER}/token",
        headers={"Content-Type": "application/x-www-form-urlencoded"},
        data={
            "grant_type": "refresh_token",
            "refresh_token": REFRESH_TOKEN,
            "client_id": IAM_CLIENT_ID,
            "client_secret": IAM_CLIENT_SECRET,
        },
        timeout=30,
    )
    resp.raise_for_status()
    data = resp.json()
    token = data.get("access_token")
    if not token:
        raise RuntimeError(f"access_token non trovato nella risposta IAM: {data}")
    return token


def upload_file(file_path: str):
    if not os.path.isfile(file_path):
        raise FileNotFoundError(file_path)

    iam_token = get_access_token()

    sts_client = boto3.client(
        "sts",
        endpoint_url=S3_ENDPOINT,
        region_name=REGION,
    )

    response = sts_client.assume_role_with_web_identity(
        RoleArn=ROLE_ARN,
        RoleSessionName="upload-session",
        DurationSeconds=3600,
        WebIdentityToken=iam_token,
    )

    creds = response["Credentials"]

    s3client = boto3.client(
        "s3",
        aws_access_key_id=creds["AccessKeyId"],
        aws_secret_access_key=creds["SecretAccessKey"],
        aws_session_token=creds["SessionToken"],
        endpoint_url=S3_ENDPOINT,
        region_name=REGION,
    )

    key = f"{KEY_PREFIX}/{os.path.basename(file_path)}"
    s3client.upload_file(file_path, BUCKET, key)

    print(f"Upload completato: s3://{BUCKET}/{key}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"Uso: {sys.argv[0]} <file>", file=sys.stderr)
        sys.exit(1)

    upload_file(sys.argv[1])
