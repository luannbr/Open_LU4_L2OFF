-- Current Cached.exe extended slots 441-450: contribution rank lists,
-- pledge-raid list/reward/inzone persistence and pledge announcements.
-- Pledge-mission load/add-count already use the callable bodies recovered in
-- migration 116 and are not redefined here.
--
-- Current machine code fixes every function name, input type/order and result
-- type/order below. No supplied database dump keeps these newer procedure
-- bodies. The relational selections and mutations are therefore explicit
-- ABI-constrained PostgreSQL models over the recovered stable
-- and cached_portable tables, not claims about unavailable MSSQL source.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION
"world"."lin_pledgecontributionmembergetperiodrankers"(
    p_pledge_id integer
) RETURNS TABLE (
    user_name text,
    pledge_type integer,
    period_point integer,
    total_point integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT contribution."user_name"::text,
           contribution."pledge_type",
           contribution."period_point",
           contribution."total_point"
      FROM "world"."pledge_contribution_member_history" AS contribution
     WHERE contribution."pledge_id" = p_pledge_id
     ORDER BY contribution."period_point" DESC,
              contribution."user_id";
$function$;

CREATE OR REPLACE FUNCTION
"world"."lin_pledgecontributionmembergetprevperiodrankers"(
    p_pledge_id integer
) RETURNS TABLE (
    period_rank integer,
    user_name text,
    pledge_type integer,
    period_point integer,
    total_point integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT contribution."period_rank",
           contribution."user_name"::text,
           contribution."pledge_type",
           contribution."period_point",
           contribution."total_point"
      FROM "world"."pledge_contribution_member_history" AS contribution
     WHERE contribution."pledge_id" = p_pledge_id
       AND contribution."period_rank" > 0
     ORDER BY contribution."period_rank",
              contribution."user_id";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_pledgeraidgetinfolist"(
    p_season integer
) RETURNS TABLE (
    world_id integer,
    pledge_db_id integer,
    pledge_name text,
    ranking integer,
    point bigint
)
LANGUAGE sql
STABLE
AS $function$
    SELECT info."world_id",
           info."pledge_db_id",
           info."pledge_name",
           info."ranking",
           info."point"
      FROM "world"."cached_portable_pledge_raid_info" AS info
     WHERE info."season" = p_season
     ORDER BY info."ranking",
              info."world_id",
              info."pledge_db_id";
$function$;

CREATE OR REPLACE FUNCTION
"world"."lin_pledgeraidupdaterewardreason"(
    p_season integer,
    p_pledge_db_id integer,
    p_reward_reason integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."cached_portable_pledge_raid_info" AS info
       SET "reward_reason" = p_reward_reason
     WHERE info."season" = p_season
       AND info."pledge_db_id" = p_pledge_db_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_pledgeraidinzoneload"()
RETURNS TABLE (
    pledge_db_id integer,
    restrict_time integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT inzone."pledge_db_id",
           inzone."restrict_time"
      FROM "world"."cached_portable_pledge_raid_inzone" AS inzone
     ORDER BY inzone."pledge_db_id";
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadpledgeannounce"(
    p_pledge_id integer
) RETURNS TABLE (
    show_flag integer,
    content text
)
LANGUAGE sql
STABLE
AS $function$
    SELECT announce."show_flag"::integer,
           COALESCE(announce."content", '')::text
      FROM "world"."pledge_announce" AS announce
     WHERE announce."pledge_id" = p_pledge_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setpledgeannounce"(
    p_pledge_id integer,
    p_show_flag integer,
    p_content text
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."pledge_announce" AS announce (
        "pledge_id", "show_flag", "content"
    ) VALUES (
        p_pledge_id, p_show_flag::smallint, COALESCE(p_content, '')
    )
    ON CONFLICT ("pledge_id") DO UPDATE
    SET "show_flag" = EXCLUDED."show_flag",
        "content" = EXCLUDED."content";
$function$;

CREATE OR REPLACE FUNCTION
"world"."lin_switchpledgeannounceshowflag"(
    p_pledge_id integer,
    p_show_flag integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."pledge_announce" AS announce
       SET "show_flag" = p_show_flag::smallint
     WHERE announce."pledge_id" = p_pledge_id;
$function$;

COMMIT;
