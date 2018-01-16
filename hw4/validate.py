import pandas as pd 
import numpy as np
Yt = np.array(pd.read_csv('ans.csv'))[:, 1]
Yp = np.array(pd.read_csv('myans.csv'))[:, 1]
print((Yt == Yp).sum() / len(Yt))