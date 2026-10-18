-- Current Cached.exe extended requests 331 through 340.
--
-- The executable fixes every callable ABI and result width below. Classic
-- 3.0, modified 287 and Ertheia keep identical relational effects for
-- all seven procedures, including the main-class user_subjob creation
-- in lin_UpdateCharMaxLevel.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_resetwartargeted"(
    p_challenger integer,
    p_challengee integer,
    p_cancel_time integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."war_declare" AS war
    SET "declare_time" = p_cancel_time,
        "death_count" = 0,
        "war_point" = 0,
        "war_point_prev" = 0
    WHERE war."challenger" = p_challenger
      AND war."challengee" = p_challengee;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadmenteestate"(
    p_char_id integer
) RETURNS TABLE (
    status integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT mentor."status"
    FROM "world"."user_mentor" AS mentor
    WHERE mentor."mentee_id" = p_char_id
      AND mentor."status" = 4
    LIMIT 1;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_checkcharmaxlevel"(
    p_char_id integer,
    p_subjob_id integer
) RETURNS TABLE (
    max_level integer
)
LANGUAGE plpgsql
STABLE
AS $function$
BEGIN
    IF p_subjob_id = 0 AND NOT EXISTS (
        SELECT 1
        FROM "world"."user_subjob" AS subjob
        WHERE subjob."char_id" = p_char_id
          AND subjob."subjob_id" = p_subjob_id
    ) THEN
        RETURN QUERY SELECT 1;
    ELSE
        RETURN QUERY
        SELECT subjob."max_level"
        FROM "world"."user_subjob" AS subjob
        WHERE subjob."char_id" = p_char_id
          AND subjob."subjob_id" = p_subjob_id;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updatecharmaxlevel"(
    p_char_id integer,
    p_subjob_id integer,
    p_level integer,
    p_force_update integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF p_subjob_id = 0 AND NOT EXISTS (
        SELECT 1
        FROM "world"."user_subjob" AS subjob
        WHERE subjob."char_id" = p_char_id
          AND subjob."subjob_id" = p_subjob_id
    ) THEN
        INSERT INTO "world"."user_subjob" (
            "char_id",
            "subjob_id",
            "hp",
            "mp",
            "sp",
            "exp",
            "level",
            "henna_1",
            "henna_2",
            "henna_3",
            "create_date",
            "max_level"
        )
        SELECT character."char_id",
               0,
               character."hp",
               character."mp",
               character."sp",
               character."exp",
               character."lev",
               COALESCE(henna."henna_1", 0),
               COALESCE(henna."henna_2", 0),
               COALESCE(henna."henna_3", 0),
               character."create_date",
               p_level
        FROM "world"."user_data" AS character
        LEFT JOIN "world"."user_henna" AS henna
          ON henna."char_id" = character."char_id"
        WHERE character."char_id" = p_char_id;
    END IF;

    UPDATE "world"."user_subjob" AS subjob
    SET "max_level" = p_level
    WHERE subjob."char_id" = p_char_id
      AND subjob."subjob_id" = p_subjob_id
      AND (
          subjob."max_level" < p_level
          OR p_force_update = 1
      );
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_brloadinterestedproduct"(
    p_char_id integer
) RETURNS TABLE (
    product_id integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT interested."product_id"
    FROM "world"."br_interested_product" AS interested
    WHERE interested."char_id" = p_char_id
    ORDER BY interested."insert_date" DESC;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_braddinterestedproduct"(
    p_char_id integer,
    p_product_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."br_interested_product" (
        "char_id",
        "product_id",
        "insert_date"
    ) VALUES (
        p_char_id,
        p_product_id,
        "world"."mssql_getdate"()
    )
    ON CONFLICT ("char_id", "product_id") DO UPDATE
    SET "insert_date" = "world"."mssql_getdate"();
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_brdeleteinterestedproduct"(
    p_char_id integer,
    p_product_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."br_interested_product" AS interested
    WHERE interested."char_id" = p_char_id
      AND (
          p_product_id = 0
          OR interested."product_id" = p_product_id
      );
$function$;

COMMIT;
