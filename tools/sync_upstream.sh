#!/bin/sh
git remote add upstream https://github.com/sony/flutter-embedded-linux.git
git remote -v

git fetch upstream

git checkout master
git merge upstream/master
