#!/bin/bash
set -e

VERSION=$(grep "^Version:" pkg/rpm/mvgal.spec | awk '{print $2}')
NAME=mvgal

# Create source tarball
git archive --prefix=${NAME}-${VERSION}/ -o ${NAME}-${VERSION}.tar.gz HEAD
echo "Created ${NAME}-${VERSION}.tar.gz"

# Build SRPM
rpmbuild -bs pkg/rpm/mvgal.spec \
  --define "_sourcedir $(pwd)" \
  --define "_srcrpmdir $(pwd)"
