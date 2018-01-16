gcc hw4.c -std=c99
./a.out < ../data/training_data > myans.csv 2>log
python3 validate.py