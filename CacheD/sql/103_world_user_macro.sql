-- Current Cached.exe primary opcode 205 and UserMacro/CUser owners.
--
-- The current machine image fixes all six call ABIs and the eleven-column
-- lin_GetMacro result order.  Classic 3.0, modified 287 and Ertheia keep
-- identical full-arity relational effects. Incompatible extender stubs are
-- excluded.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_createmacro"(
    p_char_id integer,
    p_macro_name text,
    p_macro_tooltip text,
    p_macro_iconname text,
    p_macro_icontype integer
) RETURNS TABLE (macro_id integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_macro_id integer;
BEGIN
    INSERT INTO "world"."user_macro" (
        "char_id",
        "macro_name",
        "macro_tooltip",
        "macro_iconname",
        "macro_icontype"
    ) VALUES (
        p_char_id,
        p_macro_name,
        p_macro_tooltip,
        p_macro_iconname,
        p_macro_icontype
    )
    RETURNING "user_macro"."macro_id"
    INTO v_macro_id;

    RETURN QUERY SELECT v_macro_id;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_createmacroinfo"(
    p_macro_id integer,
    p_macro_order integer,
    p_macro_int1 integer,
    p_macro_int2 integer,
    p_macro_int3 integer,
    p_macro_str text
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF EXISTS (
        SELECT 1
        FROM "world"."user_macro"
        WHERE "user_macro"."macro_id" = p_macro_id
    ) THEN
        IF EXISTS (
            SELECT 1
            FROM "world"."user_macroinfo"
            WHERE "user_macroinfo"."macro_id" = p_macro_id
              AND "user_macroinfo"."macro_order" = p_macro_order
        ) THEN
            -- The shipping donor body does not replace
            -- macro_str on this update path.
            UPDATE "world"."user_macroinfo"
            SET "macro_int1" = p_macro_int1,
                "macro_int2" = p_macro_int2,
                "macro_int3" = p_macro_int3
            WHERE "user_macroinfo"."macro_id" = p_macro_id
              AND "user_macroinfo"."macro_order" = p_macro_order;
        ELSE
            INSERT INTO "world"."user_macroinfo" (
                "macro_id",
                "macro_order",
                "macro_int1",
                "macro_int2",
                "macro_int3",
                "macro_str"
            ) VALUES (
                p_macro_id,
                p_macro_order,
                p_macro_int1,
                p_macro_int2,
                p_macro_int3,
                p_macro_str
            );
        END IF;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setmacro"(
    p_macro_id integer,
    p_macro_name text,
    p_macro_tooltip text,
    p_macro_iconname text,
    p_macro_icontype integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF EXISTS (
        SELECT 1
        FROM "world"."user_macro"
        WHERE "user_macro"."macro_id" = p_macro_id
    ) THEN
        UPDATE "world"."user_macro"
        SET "macro_name" = p_macro_name,
            "macro_tooltip" = p_macro_tooltip,
            "macro_iconname" = p_macro_iconname,
            "macro_icontype" = p_macro_icontype
        WHERE "user_macro"."macro_id" = p_macro_id;

        DELETE FROM "world"."user_macroinfo"
        WHERE "user_macroinfo"."macro_id" = p_macro_id;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_deletemacro"(
    p_macro_id integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    DELETE FROM "world"."user_macroinfo"
    WHERE "user_macroinfo"."macro_id" = p_macro_id;

    DELETE FROM "world"."user_macro"
    WHERE "user_macro"."macro_id" = p_macro_id;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getmacrocount"(
    p_char_id integer
) RETURNS TABLE (macro_count integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT count(*)::integer
    FROM "world"."user_macro"
    WHERE "user_macro"."char_id" = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getmacro"(
    p_char_id integer
) RETURNS TABLE (
    macro_id integer,
    char_id integer,
    macro_name text,
    macro_tooltip text,
    macro_iconname text,
    macro_icontype integer,
    macro_order integer,
    macro_int1 integer,
    macro_int2 integer,
    macro_int3 integer,
    macro_str text
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        root."macro_id",
        root."char_id",
        root."macro_name"::text,
        root."macro_tooltip"::text,
        root."macro_iconname"::text,
        root."macro_icontype",
        info."macro_order",
        info."macro_int1",
        info."macro_int2",
        info."macro_int3",
        info."macro_str"::text
    FROM "world"."user_macro" AS root
    LEFT JOIN "world"."user_macroinfo" AS info
      ON root."macro_id" = info."macro_id"
    WHERE root."char_id" = p_char_id
    ORDER BY root."macro_id" ASC,
             info."macro_order" ASC NULLS FIRST;
$function$;

COMMIT;
