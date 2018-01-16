gcc hw4.c -std=c99 -o hw4
./hw4 -data ../data -output submission.csv -tree 100 -thread 2
python3 validate.py submission.csv