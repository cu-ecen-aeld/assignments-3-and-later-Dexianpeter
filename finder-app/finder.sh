if [ $# -lt 2 ]
then
	    echo "Error: Missing arguments"
	        exit 1
fi

filesdir=$1
searchstr=$2

if [ ! -d "$filesdir" ]
then
	    echo "Error: $filesdir is not a directory"
	        exit 1
fi

X=$(filesadd=$(find "$filesdir" -type f | wc -l); echo "$filesadd")
Y=$(matchinglines=$(grep -r "$searchstr" "$filesdir" | wc -l); echo "$matchinglines")

echo "The number of files are $X and the number of matching lines are $Y"
