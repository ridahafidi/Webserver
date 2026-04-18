#!/usr/bin/env python3
import os, sys

print("Content-Type: text/html")
print("Status: 200 OK")
print()
print("<!DOCTYPE html>")
print("<html><head><title>CGI Test</title></head><body>")
print("<h1>CGI is working!</h1>")
print("<p>Method: " + os.environ.get("REQUEST_METHOD", "unknown") + "</p>")
print("<p>Query: " + os.environ.get("QUERY_STRING", "") + "</p>")

body = sys.stdin.read()
if body:
    print("<p>Body: " + body + "</p>")
print("</body></html>")
