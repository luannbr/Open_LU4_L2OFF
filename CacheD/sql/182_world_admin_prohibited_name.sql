-- Current Cached.exe AQ 27 persistence.
--
-- SetProhibitedPacket 0x004364f4 fixes both two-input callable ABIs and the
-- option routing. Classic 3.0, modified-287 and Ertheia keep identical
-- relational bodies over the two main prohibited-name tables.

BEGIN;

CREATE OR REPLACE FUNCTION "world"."lin_addprohibit"(
    p_char_name text,
    p_option integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF p_option = 1 THEN
        INSERT INTO "world"."user_prohibit" ("char_name")
        VALUES (p_char_name);
    ELSIF p_option = 3 THEN
        INSERT INTO "world"."user_prohibit_word" ("words")
        VALUES (p_char_name);
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_delprohibit"(
    p_char_name text,
    p_option integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF p_option = 2 THEN
        DELETE FROM "world"."user_prohibit" AS prohibited
        WHERE "world"."mssql_text_eq"(prohibited."char_name", p_char_name);
    ELSIF p_option = 4 THEN
        DELETE FROM "world"."user_prohibit_word" AS prohibited
        WHERE "world"."mssql_text_eq"(prohibited."words", p_char_name);
    END IF;
END;
$function$;

COMMIT;
