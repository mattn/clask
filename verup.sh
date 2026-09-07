#!/bin/bash

TAG=$(git tag -l --sort=v:refname | tail -1)
NEXT=v0.0.$(($(echo $TAG | sed 's/^.*\.//') + 1))
git tag $NEXT
git push origin master
git push --tags
