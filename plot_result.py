
#!/bin/python3
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
def do_plot(r, s, l):
  plt.style.use('dark_background')

  X_axis = np.array([1, 2, 3, 4])
  width = 0.25

  plt.bar(X_axis        , s, width = width, color = 'lightblue', edgecolor = 'white', label = 'Summary')
  plt.bar(X_axis + width, l, width = width, color = 'orange', edgecolor = 'white', label = 'Light')
  plt.plot([0,5], [5,5], 'w--')

  plt.xticks(X_axis + width/2, ['2', '4', '8', '16'])
  plt.yticks(list(plt.yticks()[0]) + [5])
  plt.xlabel("Threads")
  plt.ylabel("% over NMT Off")
  plt.title(f"Comparison of overhead of \nSummary and Light mode \nwith NMT Off (Regions={r} per thread)")
  plt.legend()
  plt.savefig(f'{r}_plot.png')
  plt.close('all')

df = pd.read_csv("BenchmarkResults.txt", sep = '\s+')
for r in [100, 200, 400]:
  ys = list()
  yl = list()
  for t in [2, 4, 8, 16]:
    dt = df[df["(THREADS)"] == t]
    dr = dt[dt["(REGIONS)"] == r ]
    off = float(dr[dr["Benchmark"].str.contains('Off')]["Score"])
    off -= float(dr[dr["Benchmark"].str.contains('Off')]["Error"])

    smry = float(dr[dr["Benchmark"].str.contains('Summary')]["Score"])
    smry += float(dr[dr["Benchmark"].str.contains('Summary')]["Error"])

    light = float(dr[dr["Benchmark"].str.contains('Light')]["Score"])
    light += float(dr[dr["Benchmark"].str.contains('Light')]["Error"])

    s_percent = (smry - off) / off * 100
    l_percent = (light - off) / off * 100
    ys.append(s_percent)
    yl.append(l_percent)
    print(f"{t}, {r}, {s_percent:5.4}%, {l_percent:5.4}%")
  do_plot(r, ys, yl)
import readline
readline.write_history_file('history')
