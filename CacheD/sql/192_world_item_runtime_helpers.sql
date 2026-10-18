-- Current Cached.exe item-duration, use-count, shape-shift and skill-lock
-- persistence.
--
-- Current machine paths fix every callable name and ordered input/result
-- type.  Classic 3.0, modified 287 and Ertheia agree on GetItemDuration,
-- SetSkillLock and UpdateShapeShiftedItem relational effects.  The current
-- CItem::SetApperanceType call fixes the newer shape arguments as
-- (item, appearance, period, start_time), reversing the last two names in
-- those older procedures.  UpdateItemUseCount has no surviving donor body.
-- both current CETC_MANI_USE_COUNT paths, CItem+0x100 and the admitted
-- user_item.use_count column fix its complete observable update.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_getitemduration"(
    p_item_id integer
) RETURNS TABLE (duration integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT item_duration."duration"
      FROM "world"."user_item_duration" AS item_duration
     WHERE item_duration."item_id" = p_item_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_setskilllock"(
    p_char_id integer,
    p_subjob_id integer,
    p_skill_id integer,
    p_skill_lock integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."user_skill" AS skill
       SET "is_lock" = p_skill_lock::smallint
     WHERE skill."char_id" = p_char_id
       AND skill."skill_id" = p_skill_id
       AND COALESCE(skill."subjob_id", 0) = p_subjob_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updateitemusecount"(
    p_item_id integer,
    p_use_count smallint
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."user_item" AS item
       SET "use_count" = p_use_count
     WHERE item."item_id" = p_item_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updateshapeshifteditem"(
    p_item_id integer,
    p_apperance_type integer,
    p_period integer,
    p_start_time integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF p_start_time = -1 THEN
        UPDATE "world"."user_item_apperance" AS appearance
           SET "apperance_type" = p_apperance_type,
               "period" = p_period
         WHERE appearance."item_id" = p_item_id;
    ELSE
        UPDATE "world"."user_item_apperance" AS appearance
           SET "apperance_type" = p_apperance_type,
               "start_time" = p_start_time,
               "period" = p_period
         WHERE appearance."item_id" = p_item_id;
    END IF;

    IF NOT FOUND THEN
        INSERT INTO "world"."user_item_apperance" (
            "item_id",
            "apperance_type",
            "start_time",
            "period"
        ) VALUES (
            p_item_id,
            p_apperance_type,
            p_start_time,
            p_period
        );
    END IF;
END;
$function$;

COMMIT;
