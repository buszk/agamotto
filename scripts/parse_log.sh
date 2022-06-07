#!/bin/bash

grep -a covers $1.log|awk '{print $3}' |sort -n|uniq|tail -n 1
