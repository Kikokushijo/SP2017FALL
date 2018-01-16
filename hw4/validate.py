import pandas as pd 
import numpy as np
import sys
Yt = np.array(pd.read_csv('ans.csv'))[:, 1]
Yp = np.array(pd.read_csv(sys.argv[1]))[:, 1]
print("SCORE:", (Yt == Yp).sum() / len(Yt))