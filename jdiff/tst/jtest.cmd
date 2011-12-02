jdiff -b -vv c:\data\docs1.tar c:\data\docs2.bin c:\data\docs2.jdf
jptch -v c:\data\docs1.tar c:\data\docs2.jdf c:\data\docs2.tst
FC /B /A c:\data\docs2.bin c:\data\docs2.tst

pause

jdiff -vv c:\data\doct1.tar c:\data\doct2.bin c:\data\doct2.jdf
jptch -v c:\data\doct1.tar c:\data\doct2.jdf c:\data\doct2.tst
FC /B /A c:\data\doct2.bin c:\data\doct2.tst
