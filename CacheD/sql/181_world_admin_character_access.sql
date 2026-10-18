-- Current Cached.exe AQ 18..21 administrative character-access persistence.
--
-- The current machine image fixes the four callable names and ordered input
-- ABIs. Classic 3.0, modified protocol-287 and Ertheia donors agree on each
-- database effect. No behavior is imported from the unverified HF recovery.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_deleteuserpoint"(
    p_char_id integer,
    p_point_type integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."user_point" AS point
    WHERE point."char_id" = p_char_id
      AND point."point_type" = p_point_type;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setbuilderaccount"(
    p_account_name text,
    p_default_level integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_account_id integer := 0;
BEGIN
    IF p_default_level = 0 THEN
        DELETE FROM "world"."builder_account" AS builder
        WHERE "world"."mssql_text_eq"(builder."account_name", p_account_name);
    ELSIF EXISTS (
        SELECT 1
        FROM "world"."builder_account" AS builder
        WHERE "world"."mssql_text_eq"(builder."account_name", p_account_name)
    ) THEN
        UPDATE "world"."builder_account" AS builder
        SET "default_builder" = p_default_level
        WHERE "world"."mssql_text_eq"(builder."account_name", p_account_name);
    ELSE
        SELECT character."account_id"
        INTO v_account_id
        FROM "world"."user_data" AS character
        WHERE "world"."mssql_text_eq"(
            character."account_name",
            p_account_name
        )
          AND character."account_id" > 0
        LIMIT 1;

        INSERT INTO "world"."builder_account" (
            "account_name",
            "default_builder",
            "account_id"
        ) VALUES (
            p_account_name,
            p_default_level,
            COALESCE(v_account_id, 0)
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_disablechar"(
    p_char_name text
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."user_data" AS character
    SET "account_id" = -2
    WHERE "world"."mssql_text_eq"(character."char_name", p_char_name);
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_enablechar2"(
    p_char_id integer,
    p_account_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."user_data" AS character
    SET "account_id" = p_account_id
    WHERE character."char_id" = p_char_id
      AND character."account_id" < 0;
$function$;

COMMIT;
