import numpy as np 
with open('training_data', 'r') as f:
    train_set = np.loadtxt(f.read().split('\n'))[:, 1:]
X = train_set[:, :-1]
Y = train_set[:, -1]
print(X[:5], Y[:5])