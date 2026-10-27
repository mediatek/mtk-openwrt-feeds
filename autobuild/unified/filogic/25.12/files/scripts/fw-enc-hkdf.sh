#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (C) 2025 MediaTek Inc.
#

usage() {
	printf "Usage: %s -i input key file -o hkdf key -t its " "$(basename "$0")"
	printf "\n\t-i ==> input key file"
	printf "\n\t-o ==> output key file"
	printf "\n\t-t ==> input salt file"
	exit 1
}

bin2hex() {
	od -An -t x1 -w128 | sed "s/ //g"
}

hex2bin() {
	$PERL -e 'print pack "H*", <STDIN>'
}

hkdf() {
	local key=$1
	local salt=$2
	local out=$3
	local k_derived=$(${OPENSSL} kdf -keylen 32 -kdfopt digest:SHA2-256 \
		-kdfopt hexkey:$key \
		-kdfopt hexsalt:$salt HKDF | sed "s/://g")
	echo $out
	echo -n $k_derived | hex2bin  > $out
}

while getopts "i:o:t:" OPTION
do
	case $OPTION in
	i ) KEY_PATH=$OPTARG;;
	o ) HKDF_PATH=$OPTARG;;
	t ) SALT_PATH=$OPTARG;;
	* ) echo "Invalid option passed to '$0' (options:$*)"
	usage;;
	esac
done

if [ ! -f "${KEY_PATH}" ]; then
	echo "key directory not found"
	usage;
fi

if [ ! -f "${SALT_PATH}" ]; then
	echo "salt directory not found"
	usage;
fi

if [ -z "${HOST_BIN}" -o -z "${HOSTPKG_BIN}" ]; then
	echo "bin directory not found"
	exit 1
fi

OPENSSL=$HOST_BIN/openssl3
PERL=$HOST_BIN/perl
ROE_KEY=$(cat ${KEY_PATH} | bin2hex)
SALT=$(cat ${SALT_PATH} | bin2hex)

hkdf ${ROE_KEY} ${SALT} ${HKDF_PATH}
