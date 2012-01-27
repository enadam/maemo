#!/bin/sh

# ~/MyDocs/tmp is not indexed by tracker.
[ -d ~/MyDocs/tmp ] || mkdir ~/MyDocs/tmp;
exec spycam -l ~/MyDocs/tmp/spycam-`date +%s`;
