#!/bin/bash

Dir=.
ASSMPATT=*.o
SRCPATT=*.cpp
HEADPATT=*.h

SUMASSEMBLY=0
SUMSRC=0

while read FILE; do
	echo $FILE
	MEMSET=$(grep -o -i memset $FILE | wc -l)
	MEMCPY=$(grep -o -i memcpy $FILE | wc -l)
	MEMMOVE=$(grep -o -i memmove $FILE | wc -l)
	if (( $MEMSET > 2 )); then
		MEMSET=$(($MEMSET - 2))
	else
		MEMSET=0
	fi
	if (( $MEMCPY > 2 )); then
		MEMCPY=$(($MEMCPY - 2))
	else
		MEMCPY=0
	fi
	if (( $MEMMOVE > 2 )); then
		MEMMOVE=$(($MEMMOVE - 2))
	else
		MEMMOVE=0
	fi

	SUMASSEMBLY=$((MEMSET + MEMCPY + MEMMOVE + ${SUMASSEMBLY}))
	echo "MEMSET=${MEMSET}	MEMCPY=${MEMCPY}	MEMMOVE=${MEMMOVE}	==>	SUMASSEMBLY=${SUMASSEMBLY}"
done <<<$(find $DIR -type f -name "$ASSMPATT")

while read FILE; do
	echo $FILE
	MEMSET=$(grep -o -i memset $FILE | wc -l)
	MEMCPY=$(grep -o -i memcpy $FILE | wc -l)
	MEMMOVE=$(grep -o -i memmove $FILE | wc -l)
	SUMSRC=$((MEMSET + MEMCPY + MEMMOVE + ${SUMSRC}))
	echo "MEMSET=${MEMSET}	MEMCPY=${MEMCPY}	MEMMOVE=${MEMMOVE}	==>	SUMSRC=${SUMSRC}"
done <<<$(find $DIR -type f -name "$SRCPATT")


while read FILE; do
	echo $FILE
	MEMSET=$(grep -o -i memset $FILE | wc -l)
	MEMCPY=$(grep -o -i memcpy $FILE | wc -l)
	MEMMOVE=$(grep -o -i memmove $FILE | wc -l)
	SUMSRC=$((MEMSET + MEMCPY + MEMMOVE + ${SUMSRC}))
	echo "MEMSET=${MEMSET}	MEMCPY=${MEMCPY}	MEMMOVE=${MEMMOVE}	==>	SUMSRC=${SUMSRC}"
done <<<$(find $DIR -type f -name "$HEADPATT")



echo "# Memory operations in ASSEMBLY = ${SUMASSEMBLY} # Memory operations in the code = ${SUMSRC}"
