-- Current Cached.exe primary opcode 136 pledge-war preload.
--
-- RequestLoadAllPledgeWar binds this exact six-integer result ABI. Classic
-- 3.0, modified protocol-287 and Ertheia donors have identical relational
-- effects: keep only wars whose two pledge owners still exist and map the
-- three nullable counters to zero. No donor defines a row order, so the
-- PostgreSQL carrier does not invent one.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_getwardeclare"()
RETURNS TABLE (
    challenger integer,
    challengee integer,
    declare_time integer,
    war_point integer,
    war_point_prev integer,
    death_count integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT war."challenger",
           war."challengee",
           war."declare_time",
           COALESCE(war."war_point", 0),
           COALESCE(war."war_point_prev", 0),
           COALESCE(war."death_count", 0)
    FROM "world"."war_declare" AS war
    WHERE EXISTS (
              SELECT 1
              FROM "world"."pledge" AS challenger_pledge
              WHERE challenger_pledge."pledge_id" = war."challenger"
          )
      AND EXISTS (
              SELECT 1
              FROM "world"."pledge" AS challengee_pledge
              WHERE challengee_pledge."pledge_id" = war."challengee"
          );
$function$;

COMMIT;
