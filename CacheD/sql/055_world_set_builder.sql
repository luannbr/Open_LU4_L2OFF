-- Current Cached.exe primary opcode 54 (SetBuilderPacket) persistence.
--
-- Current machine bindings fix one UTF-16 character name, one 32-bit builder
-- level and one integer result. All eight substantive full-arity-compatible
-- donor families retain this same update-and-return body.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_setbuildercharacter"(
    p_character_name text,
    p_builder_level integer
) RETURNS TABLE (character_id integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    affected_rows integer := 0;
    selected_character_id integer := 0;
BEGIN
    UPDATE "world"."user_data" AS character
    SET "builder" = p_builder_level
    WHERE "world"."mssql_text_eq"(character."char_name", p_character_name);

    GET DIAGNOSTICS affected_rows = ROW_COUNT;
    IF affected_rows > 0 THEN
        SELECT character."char_id"
        INTO selected_character_id
        FROM "world"."user_data" AS character
        WHERE "world"."mssql_text_eq"(
            character."char_name",
            p_character_name
        )
        LIMIT 1;
    END IF;

    RETURN QUERY SELECT selected_character_id;
END;
$function$;

COMMIT;
