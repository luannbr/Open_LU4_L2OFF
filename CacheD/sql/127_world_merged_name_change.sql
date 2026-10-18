-- Extended request 120 and CUser::ChangeCharacternameExtern. Current
-- Cached.exe fixes both callable ABIs. The relational validation and update
-- bodies below are shared by the compatible Ertheia/287 and Classic-family
-- SQL donors. This migration does not claim byte identity with an unavailable
-- current MSSQL procedure script.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_changecharactername2"(
    p_char_id integer,
    p_old_char_name text,
    p_new_char_name text
) RETURNS TABLE(result integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_builder integer;
    v_result integer := 0;
    v_account_name text;
    v_create_date timestamp without time zone;
BEGIN
    IF p_new_char_name LIKE ' ' THEN
        RETURN QUERY SELECT 0;
        RETURN;
    END IF;

    SELECT character."builder"
    INTO v_builder
    FROM "world"."user_data" AS character
    WHERE character."char_id" = p_char_id;

    IF v_builder = 0 THEN
        IF EXISTS (
            SELECT 1
            FROM "world"."user_prohibit" AS prohibited
            WHERE "world"."mssql_text_eq"(
                prohibited."char_name",
                p_new_char_name
            )
        ) OR EXISTS (
            SELECT 1
            FROM "world"."user_prohibit_word" AS prohibited
            WHERE strpos(
                lower(p_new_char_name),
                lower(prohibited."words")) > 0
        ) THEN
            RETURN QUERY SELECT 0;
            RETURN;
        END IF;
    END IF;

    IF EXISTS (
        SELECT 1
        FROM "world"."user_data" AS character
        WHERE "world"."mssql_text_eq"(
            character."char_name",
            p_new_char_name
        )
          AND character."char_id" <> p_char_id
    ) THEN
        RETURN QUERY SELECT 0;
        RETURN;
    END IF;

    UPDATE "world"."user_data" AS character
    SET "char_name" = p_new_char_name
    WHERE "world"."mssql_text_eq"(
        character."char_name",
        p_old_char_name
    );

    IF FOUND THEN
        SELECT character."char_id",
               character."account_name",
               character."create_date"
        INTO v_result, v_account_name, v_create_date
        FROM "world"."user_data" AS character
        WHERE "world"."mssql_text_eq"(
            character."char_name",
            p_new_char_name
        )
        LIMIT 1;

        UPDATE "world"."user_comment" AS comment
        SET "char_name" = p_new_char_name
        WHERE comment."char_id" = v_result;

        UPDATE "world"."changed_name_by_merge" AS merged
        SET "name_new" = p_new_char_name,
            "change_date" = "world"."mssql_getdate"(),
            "change_flag" = 1
        WHERE merged."id" = v_result
          AND merged."type" = 0;

        INSERT INTO "world"."user_history" (
            "char_name",
            "char_id",
            "log_action",
            "account_name",
            "create_date"
        ) VALUES (
            p_new_char_name,
            v_result,
            3,
            v_account_name,
            v_create_date
        );

        INSERT INTO "world"."user_data_extra" (
            "char_id",
            "modified_date"
        ) VALUES (
            v_result,
            "world"."mssql_getdate"()
        )
        ON CONFLICT ON CONSTRAINT "pk_user_data_extra" DO UPDATE
        SET "modified_date" = EXCLUDED."modified_date";
    END IF;

    RETURN QUERY SELECT v_result;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setnewnameformerged"(
    p_type integer,
    p_dbid integer,
    p_name text
) RETURNS TABLE(result integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_result integer := 0;
BEGIN
    IF p_type <> 1 OR p_name LIKE ' ' THEN
        RETURN QUERY SELECT 0;
        RETURN;
    END IF;

    IF EXISTS (
        SELECT 1
        FROM "world"."user_prohibit" AS prohibited
        WHERE "world"."mssql_text_eq"(prohibited."char_name", p_name)
    ) OR EXISTS (
        SELECT 1
        FROM "world"."user_prohibit_word" AS prohibited
        WHERE strpos(lower(p_name), lower(prohibited."words")) > 0
    ) OR EXISTS (
        SELECT 1
        FROM "world"."changed_name_by_merge" AS merged
        WHERE merged."type" = 1
          AND "world"."mssql_text_eq"(merged."name_new", p_name)
    ) OR EXISTS (
        SELECT 1
        FROM "world"."pledge" AS pledge
        WHERE "world"."mssql_text_eq"(pledge."name", p_name)
    ) THEN
        RETURN QUERY SELECT 0;
        RETURN;
    END IF;

    UPDATE "world"."changed_name_by_merge" AS merged
    SET "name_temp" = merged."name_new",
        "name_new" = p_name,
        "change_flag" = 2,
        "change_date" = "world"."mssql_getdate"()
    WHERE merged."id" = p_dbid
      AND merged."type" = p_type
      AND merged."change_flag" = 0;
    GET DIAGNOSTICS v_result = ROW_COUNT;

    RETURN QUERY SELECT CASE WHEN v_result > 0 THEN 1 ELSE 0 END;
END;
$function$;

COMMIT;
