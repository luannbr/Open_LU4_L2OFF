-- Current Cached.exe extended requests 291 through 300.
--
-- The executable fixes the six callable ABIs below.  Classic 3.0, modified
-- 287 and Ertheia keep matching relational bodies for all mentor and castle
-- operations.  ShapeShift requests 296 through 299 persist through the
-- already-recovered CItem::SetApperanceType/lin_UpdateShapeShiftedItem path
-- and therefore introduce no additional SQL callable here.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_loadmentor"(
    p_char_id integer
) RETURNS TABLE (
    mentor_id integer,
    mentor_name text,
    mentor_class integer,
    mentor_level integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        mentor."mentor_id",
        mentor_user."char_name"::text,
        mentor_user."class",
        mentor_user."lev"::integer
    FROM "world"."user_mentor" AS mentor
    INNER JOIN "world"."user_data" AS mentor_user
        ON mentor_user."char_id" = mentor."mentor_id"
    WHERE mentor."mentee_id" = p_char_id
      AND mentor."status" = 1;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updatementeeplaytime"(
    p_mentor_id integer,
    p_mentee_id integer,
    p_playtime integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."user_mentor" AS mentor
    SET "mentee_playtime" = mentor."mentee_playtime" + p_playtime
    WHERE mentor."mentor_id" = p_mentor_id
      AND mentor."mentee_id" = p_mentee_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updatementorplaytime"(
    p_mentor_id integer,
    p_playtime integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."user_mentor" AS mentor
    SET "mentor_playtime" = mentor."mentor_playtime" + p_playtime
    WHERE mentor."mentor_id" = p_mentor_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updatementorstatus"(
    p_mentor_id integer,
    p_mentee_id integer,
    p_status integer,
    p_end_time integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."user_mentor" AS mentor
    SET "status" = p_status,
        "end_time" = p_end_time
    WHERE mentor."mentor_id" = p_mentor_id
      AND mentor."mentee_id" = p_mentee_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_resetmentorpenalty"(
    p_mentor_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."user_mentor" AS mentor
    SET "end_time" = 0
    WHERE mentor."mentor_id" = p_mentor_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setcastlecampaign"(
    p_castle_id integer,
    p_campaign_status integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."castle" AS castle
    SET "campaign_success" = p_campaign_status
    WHERE castle."id" = p_castle_id;
$function$;

COMMIT;
