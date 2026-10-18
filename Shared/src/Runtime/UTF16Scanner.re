// Common re2c configuration for the scanners derived from
// grammar::UTF16Scanner. Include it at the start of the scanner block.
// The scanner reads UTF-16 code units through the custom input API, so it
// can scan a buffer without a terminating sentinel. YYFILL asks a reader
// for one more code unit when the scanner reaches the end of its input.
/*!re2c
    re2c:api:style = free-form;
    re2c:define:YYCTYPE = char16_t;
    re2c:define:YYPEEK = "Peek()";
    re2c:define:YYSKIP = "++cursor_;";
    re2c:define:YYBACKUP = "marker_ = cursor_;";
    re2c:define:YYRESTORE = "cursor_ = marker_;";
    re2c:define:YYBACKUPCTX = "ctxmarker_ = cursor_;";
    re2c:define:YYRESTORECTX = "cursor_ = ctxmarker_;";
    re2c:define:YYSHIFT = "cursor_ += @@;";
    re2c:define:YYLESSTHAN = "(limit_ - cursor_) < @@";
    re2c:define:YYGETCONDITION = "yystart";
    re2c:define:YYSETCONDITION = "yystart = @@;";
    re2c:define:YYFILL = "Fill() == 0";
    re2c:eof = 0;
*/
