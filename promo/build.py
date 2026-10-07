# Builds prototype.html: the template plus the two fonts, embedded so the file is fully self-contained.
import base64, pathlib
here = pathlib.Path(__file__).parent
def b64(p): return base64.b64encode((here / p).read_bytes()).decode()
fonts = f"""@font-face {{ font-family: "Archivo"; font-weight: 100 900; font-stretch: 62% 125%; src: url(data:font/woff2;base64,{b64('fonts/archivo.woff2')}) format("woff2"); }}
@font-face {{ font-family: "JetBrains Mono"; font-weight: 100 800; src: url(data:font/woff2;base64,{b64('fonts/jbmono.woff2')}) format("woff2"); }}"""
html = (here / 'template.html').read_text().replace('/*FONTS*/', fonts)
(here / 'prototype.html').write_text(html)
print('prototype.html', round(len(html) / 1024), 'KB')
