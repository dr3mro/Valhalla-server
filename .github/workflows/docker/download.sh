#!/usr/bin/env bash
#
# gh-dl-release! It works!
#
# This script downloads an asset from latest or specific Github release of a
# private repo. Feel free to extract more of the variables into command line
# parameters.
#
# PREREQUISITES
#
# curl, wget, jq
#
# USAGE
#
# Set all the variables inside the script, make sure you chmod +x it, then
# to download specific version to my_app.tar.gz:
#
#     gh-dl-release 2.1.1
#
# to download latest version:
#
#     gh-dl-release latest
#
# If your version/tag doesn't match, the script will exit with error.
set -x
TOKEN="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"
REPO="dr3mro/Valhalla-server"
FILE="server-x86_64.tar.gz"      # the name of your release asset file, e.g. build.tar.gz
VERSION="latest"                       # tag name or the word "latest"
GITHUB="https://api.github.com"

alias errcho='>&2 echo'

function gh_curl() {
  curl -H "Authorization: token $TOKEN" \
       -H "Accept: application/vnd.github.v3.raw" \
       $@
}

if [ "$VERSION" = "latest" ]; then
  # Github should return the latest release first.
  parser=".[0].assets | map(select(.name == \"$FILE\"))[0].id"
else
  parser=". | map(select(.tag_name == \"$VERSION\"))[0].assets | map(select(.name == \"$FILE\"))[0].id"
fi;

asset_id=`gh_curl -s $GITHUB/repos/$REPO/releases | jq "$parser"`
if [ "$asset_id" = "null" ]; then
  errcho "ERROR: version not found $VERSION"
  exit 1
fi;

wget -q --auth-no-challenge --header='Accept:application/octet-stream' \
  https://$TOKEN:@api.github.com/repos/$REPO/releases/assets/$asset_id \
  -O $FILE
