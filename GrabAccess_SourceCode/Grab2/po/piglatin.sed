/^Project-Id-Version:/ b
/^Report-Msgid-Bugs-To:/ b
/^POT-Creation-Date:/ b
/^PO-Revision-Date:/ b
/^Last-Translator:/ b
/^Language-Team:/ b
/^Language:/ b
/^MIME-Version:/ b
/^Content-Type:/ b
/^Content-Transfer-Encoding:/ b
/^Plural-Forms:/ b

s/"\([^"]*\)"/“\1”/g
s/`\([^`']*\)'/‘\1’/g
s/ '\([^`']*\)' / ‘\1’ /g
s/ '\([^`']*\)'$/ ‘\1’/g
s/^'\([^`']*\)' /‘\1’ /g
s/“”/""/g

s,\(^\|[^a-zA-Z0-9%\\]\)\([aoeuiAOEUI]\)\([a-zA-Z]*\),\1\2\3way,g
s,\(^\|[^a-zA-Z0-9%\\]\)\([bcdfghj-np-tvwxzBCDFGHJ-NP-TVWXZ][bcdfghj-np-tvwxzBCDFGHJ-NP-TVWXZ]*\)\([a-zA-Z]*\),\1\3\2ay,g
