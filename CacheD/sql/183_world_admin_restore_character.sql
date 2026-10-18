-- Current Cached.exe AQ 31/35 lin_RestoreChar persistence.
--
-- Current machine code fixes the ordered (account_id, char_id, account_name)
-- ABI. C1, Classic 3.0 and modified 287 keep the same body. The trailing
-- space in their quoted MSSQL procedure identifier previously prevented the
-- donor inventory from classifying it as the current spelling.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_restorechar"(
    p_account_id integer,
    p_char_id integer,
    p_account_name text DEFAULT ''
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_old_character_name text;
    v_new_character_name text;
    v_separator integer;
BEGIN
    SELECT character."char_name"
    INTO v_old_character_name
    FROM "world"."user_data" AS character
    WHERE character."char_id" = p_char_id;

    v_separator := strpos(v_old_character_name, '_');
    IF v_separator > 0 THEN
        v_new_character_name := substring(
            v_old_character_name FROM 1 FOR v_separator - 1);
    ELSE
        v_new_character_name := v_old_character_name;
    END IF;

    UPDATE "world"."user_data" AS character
    SET "account_id" = p_account_id,
        "char_name" = v_new_character_name,
        "account_name" = CASE
            WHEN length(p_account_name) > 0 THEN p_account_name
            ELSE character."account_name"
        END,
        "temp_delete_date" = NULL
    WHERE character."char_id" = p_char_id;
END;
$function$;

COMMIT;
