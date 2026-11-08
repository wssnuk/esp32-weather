import urllib.request
UA = {'User-Agent': 'esp32-weather-radar/1.0 (+https://github.com/wssnuk/esp32-weather)'}
for u in ['https://www.rainviewer.com/files/rainviewer_api_colors_table.csv']:
    try:
        with urllib.request.urlopen(urllib.request.Request(u, headers=UA), timeout=30) as r:
            t = r.read().decode()
        lines = t.splitlines()
        print('HEADER', lines[0])
        for l in lines[1:]:
            print('ROW', l)
    except Exception as e:
        print('ERR', u, e)
