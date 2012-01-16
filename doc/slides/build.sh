#!/bin/bash

set -e

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" > /dev/null && pwd)
cd "$HERE"

pdflatex pm-lab-tools-slides.tex
