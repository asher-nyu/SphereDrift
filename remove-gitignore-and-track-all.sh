#!/bin/bash
rm .gitignore
git rm -r --cached .
git add .
git commit -m "Remove .gitignore and track all files"
git push origin master:main
