-- Current Cached.exe CPledgeDb load persistence.
--
-- The current machine fixes all seven callable names, input types and result
-- bindings.  Its CPledgeDbHelper::BindProperty at 0x00511118 additionally
-- fixes the complete 27-column order of both pledge-load procedures.
-- Classic 3.0, modified 287 and Ertheia keep identical
-- academy, academy-member, pledge-count and pledge-power bodies.  They also
-- keep the common 26-column pledge/user join.  The current procedure name,
-- current CPledge fields and admitted current-era pledge layout fix its two
-- changes: old emblem_id is replaced by new_emblem_id and
-- mastery_reset_time is appended after world_id.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_getacademyname"(
    p_pledge_id integer
) RETURNS TABLE (academy_name text)
LANGUAGE sql
STABLE
AS $function$
    SELECT academy."name"::text
      FROM "world"."academy" AS academy
     WHERE academy."pledge_id" = p_pledge_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getpledgecountbyid"(
    p_pledge_id integer
) RETURNS TABLE (pledge_count integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT count(*)::integer
      FROM "world"."pledge" AS pledge
     WHERE pledge."pledge_id" = p_pledge_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getpledgecountbyname"(
    p_pledge_name text
) RETURNS TABLE (pledge_count integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT count(*)::integer
      FROM "world"."pledge" AS pledge
     WHERE "world"."mssql_text_eq"(
               pledge."name",
               p_pledge_name
           );
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getpledgepower"(
    p_pledge_id integer
) RETURNS TABLE (
    grade_id integer,
    power_bits integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT power."grade_id",
           power."power_bits"
      FROM "world"."pledge_power" AS power
     WHERE power."pledge_id" = p_pledge_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadacademymember"(
    p_pledge_id integer
) RETURNS TABLE (
    user_id integer,
    join_level integer,
    master_id integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT member."user_id",
           member."join_level",
           member."master_id"
      FROM "world"."academy_member" AS member
     WHERE member."pledge_id" = p_pledge_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadpledgebyidwithnewemblem"(
    p_pledge_id integer
) RETURNS TABLE (
    pledge_id integer,
    pledge_name text,
    ruler_id integer,
    ruler_name text,
    alliance_id integer,
    challenge_time integer,
    now_war_id integer,
    name_value integer,
    oust_time integer,
    skill_level integer,
    private_flag integer,
    status integer,
    rank integer,
    castle_id integer,
    agit_id integer,
    fortress_id integer,
    root_name_value integer,
    crest_id integer,
    is_guilty integer,
    dismiss_reserved_time integer,
    alliance_ousted_time integer,
    alliance_withdraw_time integer,
    alliance_dismiss_time integer,
    new_emblem_id integer,
    castle_siege_defence_count integer,
    world_id integer,
    mastery_reset_time integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT pledge."pledge_id",
           pledge."name"::text,
           pledge."ruler_id",
           ruler."char_name"::text,
           pledge."alliance_id",
           pledge."challenge_time",
           pledge."now_war_id",
           pledge."name_value",
           pledge."oust_time",
           pledge."skill_level"::integer,
           pledge."private_flag",
           pledge."status",
           pledge."rank",
           pledge."castle_id",
           pledge."agit_id",
           pledge."fortress_id",
           pledge."root_name_value",
           pledge."crest_id",
           pledge."is_guilty",
           pledge."dismiss_reserved_time",
           pledge."alliance_ousted_time",
           pledge."alliance_withdraw_time",
           pledge."alliance_dismiss_time",
           pledge."new_emblem_id",
           pledge."castle_siege_defence_count",
           pledge."world_id",
           pledge."mastery_reset_time"
      FROM "world"."pledge" AS pledge
      JOIN "world"."user_data" AS ruler
        ON ruler."char_id" = pledge."ruler_id"
       AND ruler."pledge_id" = p_pledge_id
       AND ruler."world"::integer = pledge."world_id"
     WHERE pledge."pledge_id" = p_pledge_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadpledgebyname"(
    p_pledge_name text
) RETURNS TABLE (
    pledge_id integer,
    pledge_name text,
    ruler_id integer,
    ruler_name text,
    alliance_id integer,
    challenge_time integer,
    now_war_id integer,
    name_value integer,
    oust_time integer,
    skill_level integer,
    private_flag integer,
    status integer,
    rank integer,
    castle_id integer,
    agit_id integer,
    fortress_id integer,
    root_name_value integer,
    crest_id integer,
    is_guilty integer,
    dismiss_reserved_time integer,
    alliance_ousted_time integer,
    alliance_withdraw_time integer,
    alliance_dismiss_time integer,
    new_emblem_id integer,
    castle_siege_defence_count integer,
    world_id integer,
    mastery_reset_time integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT loaded.*
      FROM "world"."lin_loadpledgebyidwithnewemblem"(
               COALESCE(
                   (
                       SELECT pledge."pledge_id"
                         FROM "world"."pledge" AS pledge
                        WHERE "world"."mssql_text_eq"(
                                  pledge."name",
                                  p_pledge_name
                              )
                        LIMIT 1
                   ),
                   0
               )
           ) AS loaded;
$function$;

COMMIT;
