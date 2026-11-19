#!/bin/sh

trap 'printf "VT %s interrupted\n" "$1"; exit 130' INT
printf 'VT %s ready\n' "$1"
count=0
while IFS= read -r line; do
	count=$((count + 1))
	printf 'VT %s reply %d: %s\n' "$1" "$count" "$line"
done
