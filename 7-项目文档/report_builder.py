#!/usr/bin/env python3
"""Read report content from report_data.json and generate .docx"""
import zipfile, os, json
from xml.sax.saxutils import escape as xml_escape

OUTPUT = '/mnt/c/Users/qybs/Desktop/多端协同多模态智能图书管理系统-技术报告.docx'

DOCUMENT_XML = '''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"
            xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">
  <w:body>
    {body}
    <w:sectPr>
      <w:pgSz w:w="11906" w:h="16838"/>
      <w:pgMar w:top="1440" w:right="1440" w:bottom="1440" w:left="1440" w:header="720" w:footer="720"/>
    </w:sectPr>
  </w:body>
</w:document>'''

def w_p(text, bold=False, size=24, align=None, font_ascii='Times New Roman', font_cjk='SimSun', spacing=360, first_line=0):
    rpr = f'<w:rFonts w:ascii="{font_ascii}" w:eastAsia="{font_cjk}" w:hAnsi="{font_ascii}"/>'
    rpr += f'<w:sz w:val="{size}"/><w:szCs w:val="{size}"/>'
    if bold:
        rpr += '<w:b/><w:bCs/>'
    ppr = ''
    if align:
        ppr += f'<w:jc w:val="{align}"/>'
    if first_line:
        ppr += f'<w:ind w:firstLine="{first_line}" w:left="0"/>'
    ppr += f'<w:spacing w:line="{spacing}" w:lineRule="auto" w:before="60" w:after="60"/>'
    run = f'<w:r><w:rPr>{rpr}</w:rPr><w:t xml:space="preserve">{xml_escape(text)}</w:t></w:r>'
    if ppr:
        return f'<w:p><w:pPr>{ppr}</w:pPr>{run}</w:p>'
    return f'<w:p>{run}</w:p>'

def w_heading(text, level=1):
    sizes = {0: 44, 1: 32, 2: 28, 3: 26}
    return w_p(text, bold=True, size=sizes.get(level, 28), font_cjk='SimHei', font_ascii='Arial', spacing=360)

def w_code(text):
    out = []
    for line in text.strip().split('\n'):
        out.append(w_p(line, size=18, font_ascii='Consolas', font_cjk='SimSun', spacing=240))
    return '\n'.join(out)

def w_empty():
    return '<w:p><w:pPr><w:spacing w:line="240" w:lineRule="auto"/></w:pPr></w:p>'

def w_page_break():
    return '<w:p><w:r><w:br w:type="page"/></w:r></w:p>'

def w_table(headers, rows):
    ncols = len(headers)
    w_col = 9000 // ncols
    tbl = f'<w:tbl><w:tblPr><w:tblStyle w:val="TableGrid"/><w:tblW w:w="9000" w:type="dxa"/><w:jc w:val="center"/></w:tblPr><w:tblGrid>'
    for _ in range(ncols):
        tbl += f'<w:gridCol w:w="{w_col}"/>'
    tbl += '</w:tblGrid>'
    tbl += '<w:tr>'
    for h in headers:
        tbl += f'<w:tc><w:tcPr><w:tcW w:w="{w_col}" w:type="dxa"/></w:tcPr>'
        hp = w_p(h, bold=True, size=20, align='center')
        hp = hp.replace('<w:p>', '<w:p><w:pPr><w:jc w:val="center"/></w:pPr>')
        tbl += hp + '</w:tc>'
    tbl += '</w:tr>'
    for row in rows:
        tbl += '<w:tr>'
        for cell in row:
            tbl += f'<w:tc><w:tcPr><w:tcW w:w="{w_col}" w:type="dxa"/></w:tcPr>'
            tbl += w_p(str(cell), size=20, spacing=240) + '</w:tc>'
        tbl += '</w:tr>'
    tbl += '</w:tbl>'
    return tbl

def build_docx(data):
    body = []
    for item in data:
        t = item['type']
        if t == 'empty':
            body.append(w_empty())
        elif t == 'page_break':
            body.append(w_page_break())
        elif t == 'heading':
            body.append(w_heading(item['text'], item.get('level', 1)))
        elif t == 'para':
            body.append(w_p(item['text'],
                bold=item.get('bold', False),
                first_line=480 if item.get('indent', True) else 0))
        elif t == 'code':
            body.append(w_code(item['text']))
        elif t == 'table':
            body.append(w_table(item['headers'], item['rows']))
        elif t == 'title':
            body.append(w_p(item['text'], bold=True, size=item.get('size', 48),
                align='center', font_cjk='SimHei'))
        else:
            print(f'Unknown type: {t}')

    doc_xml = DOCUMENT_XML.format(body='\n'.join(body))

    ct = '<?xml version="1.0" encoding="UTF-8" standalone="yes"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/></Types>'
    rels = '<?xml version="1.0" encoding="UTF-8" standalone="yes"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/></Relationships>'
    dr = '<?xml version="1.0" encoding="UTF-8" standalone="yes"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"></Relationships>'

    os.makedirs(os.path.dirname(OUTPUT) if os.path.dirname(OUTPUT) else '.', exist_ok=True)
    with zipfile.ZipFile(OUTPUT, 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr('[Content_Types].xml', ct)
        z.writestr('_rels/.rels', rels)
        z.writestr('word/_rels/document.xml.rels', dr)
        z.writestr('word/document.xml', doc_xml)
    print(f'Done: {OUTPUT} ({os.path.getsize(OUTPUT)} bytes)')

if __name__ == '__main__':
    with open(os.path.join(os.path.dirname(__file__), 'report_data.json'), 'r', encoding='utf-8') as f:
        data = json.load(f)
    build_docx(data)
